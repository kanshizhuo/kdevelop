/*
    This file is part of KDevelop

    Copyright 2013 Olivier de Gaalon <olivier.jg@gmail.com>
    Copyright 2013 Milian Wolff <mail@milianw.de>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "clangparsejob.h"

#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iproject.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/idocumentcontroller.h>

#include <language/interfaces/icodehighlighting.h>

#include <language/backgroundparser/urlparselock.h>
#include <language/backgroundparser/backgroundparser.h>

#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/duchain.h>
#include <language/duchain/parsingenvironment.h>

#include <custom-definesandincludes/idefinesandincludesmanager.h>

#include <project/projectmodel.h>
#include <project/interfaces/ibuildsystemmanager.h>

#include "duchain/clanghelpers.h"
#include "duchain/clangpch.h"
#include "duchain/tuduchain.h"
#include "duchain/parsesession.h"
#include "duchain/clangindex.h"
#include "duchain/clangparsingenvironmentfile.h"
#include "util/clangdebug.h"
#include "util/clangtypes.h"

#include "clangsupport.h"
#include "documentfinderhelpers.h"

#include <QFile>
#include <QStringList>
#include <QFileInfo>
#include <QReadLocker>
#include <QProcess>
#include <memory>

using namespace KDevelop;

namespace {

QString findConfigFile(const QString& forFile, const QString& configFileName)
{
    QDir dir = QFileInfo(forFile).dir();
    while (dir.exists()) {
        const QFileInfo customIncludePaths(dir, configFileName);
        if (customIncludePaths.exists()) {
            return customIncludePaths.absoluteFilePath();
        }

        if (!dir.cdUp()) {
            break;
        }
    }

    return {};
}

Path::List readPathListFile(const QString& filepath)
{
    if (filepath.isEmpty()) {
        return {};
    }

    QFile f(filepath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString text = QString::fromLocal8Bit(f.readAll());
    const QStringList lines = text.split('\n', QString::SkipEmptyParts);
    Path::List paths(lines.length());
    std::transform(lines.begin(), lines.end(), paths.begin(), [] (const QString& line) { return Path(line); });
    return paths;
}

/**
 * File should contain the header to precompile and use while parsing
 * @returns the first path in the file
 */
Path userDefinedPchIncludeForFile(const QString& sourcefile)
{
    static const QString pchIncludeFilename = QStringLiteral(".kdev_pch_include");
    const auto paths = readPathListFile(findConfigFile(sourcefile, pchIncludeFilename));
    return paths.isEmpty() ? Path() : paths.first();
}

ProjectFileItem* findProjectFileItem(const IndexedString& url, bool* hasBuildSystemInfo)
{
    ProjectFileItem* file = nullptr;

    *hasBuildSystemInfo = false;
    for (auto project: ICore::self()->projectController()->projects()) {
        auto files = project->filesForPath(url);
        if (files.isEmpty()) {
            continue;
        }

        file = files.last();

        // A file might be defined in different targets.
        // Prefer file items defined inside a target with non-empty includes.
        for (auto f: files) {
            if (!dynamic_cast<ProjectTargetItem*>(f->parent())) {
                continue;
            }
            file = f;
            if (!IDefinesAndIncludesManager::manager()->includes(f, IDefinesAndIncludesManager::ProjectSpecific).isEmpty()) {
                break;
            }
        }
    }
    if (file && file->project()) {
        if (auto bsm = file->project()->buildSystemManager()) {
            *hasBuildSystemInfo = bsm->hasIncludesOrDefines(file);
        }
    }
    return file;
}

ClangParsingEnvironmentFile* parsingEnvironmentFile(const TopDUContext* context)
{
    return dynamic_cast<ClangParsingEnvironmentFile*>(context->parsingEnvironmentFile().data());
}

bool hasTracker(const IndexedString& url)
{
    return ICore::self()->languageController()->backgroundParser()->trackerForUrl(url);
}

}

ClangParseJob::ClangParseJob(const IndexedString& url, ILanguageSupport* languageSupport)
    : ParseJob(url, languageSupport)
{
    const auto tuUrl = clang()->index()->translationUnitForUrl(url);
    bool hasBuildSystemInfo;
    if (auto file = findProjectFileItem(tuUrl, &hasBuildSystemInfo)) {
        m_environment.addIncludes(IDefinesAndIncludesManager::manager()->includes(file));
        m_environment.addDefines(IDefinesAndIncludesManager::manager()->defines(file));
    } else {
        m_environment.addIncludes(IDefinesAndIncludesManager::manager()->includes(tuUrl.str()));
        m_environment.addDefines(IDefinesAndIncludesManager::manager()->defines(tuUrl.str()));
    }
    const bool isSource = ClangHelpers::isSource(tuUrl.str());
    m_environment.setQuality(
        isSource ? (hasBuildSystemInfo ? ClangParsingEnvironment::BuildSystem : ClangParsingEnvironment::Source)
        : ClangParsingEnvironment::Unknown
    );
    m_environment.setTranslationUnitUrl(tuUrl);

    Path::List projectPaths;
    const auto& projects = ICore::self()->projectController()->projects();
    projectPaths.reserve(projects.size());
    foreach (auto project, projects) {
        projectPaths.append(project->path());
    }
    m_environment.setProjectPaths(projectPaths);

    foreach(auto document, ICore::self()->documentController()->openDocuments()) {
        auto textDocument = document->textDocument();
        if (!textDocument || !textDocument->isModified() || !textDocument->url().isLocalFile()
            || !DocumentFinderHelpers::mimeTypesList().contains(textDocument->mimeType()))
        {
            continue;
        }
        m_unsavedFiles << UnsavedFile(textDocument->url().toLocalFile(), textDocument->textLines(textDocument->documentRange()));
        const IndexedString indexedUrl(textDocument->url());
        m_unsavedRevisions.insert(indexedUrl, ModificationRevision::revisionForFile(indexedUrl));
    }
}

ClangSupport* ClangParseJob::clang() const
{
    return static_cast<ClangSupport*>(languageSupport());
}

void ClangParseJob::run(ThreadWeaver::JobPointer /*self*/, ThreadWeaver::Thread */*thread*/)
{
    QReadLocker parseLock(languageSupport()->parseLock());

    if (abortRequested()) {
        return;
    }

    {
        const auto tuUrlStr = m_environment.translationUnitUrl().str();
        m_environment.addIncludes(IDefinesAndIncludesManager::manager()->includesInBackground(tuUrlStr));
        m_environment.addDefines(IDefinesAndIncludesManager::manager()->definesInBackground(tuUrlStr));
        m_environment.setPchInclude(userDefinedPchIncludeForFile(tuUrlStr));
    }

    if (abortRequested()) {
        return;
    }

    // try to find existing session data
    ParseSessionData::Ptr existingData;
    {
        UrlParseLock urlLock(document());
        if (abortRequested() || !isUpdateRequired(ParseSession::languageString())) {
            return;
        }
        DUChainWriteLocker lock;
        // TODO: share the session data / AST between all files that are pinned to a given TU
        const auto& context = DUChainUtils::standardContextForUrl(document().toUrl());
        if (context) {
            existingData = ParseSessionData::Ptr(dynamic_cast<ParseSessionData*>(context->ast().data()));
        }
    }

    if (abortRequested()) {
        return;
    }

    ParseSession session(existingData ? existingData : createSessionData());
    const bool update = existingData && session.environment().translationUnitUrl() == m_environment.translationUnitUrl();
    if (!update || !session.reparse(m_unsavedFiles, m_environment)) {
        session.setData(createSessionData());
    }

    if (abortRequested() || !session.unit()) {
        return;
    }

    Imports imports = ClangHelpers::tuImports(session.unit());
    if (m_environment.quality() != ClangParsingEnvironment::Unknown) {
        clang()->index()->setTranslationUnitImports(m_environment.translationUnitUrl(), imports);
    }

    IncludeFileContexts includedFiles;
    if (auto pch = clang()->index()->pch(m_environment)) {
        auto pchFile = pch->mapFile(session.unit());
        includedFiles = pch->mapIncludes(session.unit());
        includedFiles.insert(pchFile, pch->context());
        auto tuFile = clang_getFile(session.unit(), m_environment.translationUnitUrl().byteArray());
        imports.insert(tuFile, { pchFile, CursorInRevision(0, 0) } );
    }

    if (abortRequested()) {
        return;
    }

    auto context = ClangHelpers::buildDUChain(session.file(), imports, session,
                                              minimumFeatures(), includedFiles, clang()->index());
    setDuChain(context);

    if (abortRequested()) {
        return;
    }

    {
        DUChainWriteLocker lock;
        if (::hasTracker(document()) || minimumFeatures() & TopDUContext::AST) {
            // cache the parse session and the contained translation unit for this chain
            // this then allows us to quickly reparse the document if it is changed by
            // the user
            // otherwise no editor component is open for this document and we can dispose
            // the TU to save memory
            context->setAst(IAstContainer::Ptr(session.data()));
        }
        auto file = parsingEnvironmentFile(context);
        Q_ASSERT(file);
        // verify that features and environment where properly set in ClangHelpers::buildDUChain
        Q_ASSERT(file->featuresSatisfied(TopDUContext::Features(minimumFeatures() & ~TopDUContext::ForceUpdateRecursive)));
        Q_UNUSED(file);
    }

    // release the data here, so we don't lock it while highlighting
    session.setData({});

    foreach(const auto& context, includedFiles) {
        if (!context) {
            continue;
        }
        {
            // prefer the editor modification revision, instead of the on-disk revision
            auto it = m_unsavedRevisions.find(context->url());
            if (it != m_unsavedRevisions.end()) {
                DUChainWriteLocker lock;
                auto file = parsingEnvironmentFile(context);
                Q_ASSERT(file);
                file->setModificationRevision(it.value());
            }
        }
        if (::hasTracker(context->url())) {
            languageSupport()->codeHighlighting()->highlightDUChain(context);
        }
    }
}

ParseSessionData::Ptr ClangParseJob::createSessionData() const
{
    const bool skipFunctionBodies = (minimumFeatures() <= TopDUContext::VisibleDeclarationsAndContexts);
    return ParseSessionData::Ptr(new ParseSessionData(m_unsavedFiles, clang()->index(), m_environment,
                                 (skipFunctionBodies ? ParseSessionData::SkipFunctionBodies : ParseSessionData::NoOption)));
}

const ParsingEnvironment* ClangParseJob::environment() const
{
    return &m_environment;
}
