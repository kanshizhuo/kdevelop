/*
 * This file is part of KDevelop
 *
 * Copyright 2006 Adam Treat <treat@kde.org>
 * Copyright 2007 Kris Wong <kris.p.wong@gmail.com>
 * Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "backgroundparser.h"

#include <QList>
#include <QFile>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QMutexLocker>
#include <QThread>

#include <kdebug.h>
#include <kglobal.h>
#include <kconfiggroup.h>
#include <ksharedconfig.h>
#include <klocale.h>

#include <ktexteditor/smartrange.h>
#include <ktexteditor/smartinterface.h>
#include <ktexteditor/document.h>

#include <threadweaver/State.h>
#include <threadweaver/ThreadWeaver.h>
#include <threadweaver/JobCollection.h>
#include <threadweaver/DebuggingAids.h>

#include <interfaces/ilanguagecontroller.h>
#include <interfaces/ilanguage.h>

#include "../interfaces/ilanguagesupport.h"

#include "parsejob.h"
#include "parserdependencypolicy.h"

namespace KDevelop
{

class BackgroundParserPrivate
{
public:
    BackgroundParserPrivate(BackgroundParser *parser, ILanguageController *languageController)
        :m_parser(parser), m_languageController(languageController)
    {
        m_timer.setSingleShot(true);
        m_delay = 500;
        m_threads = 1;
        m_doneParseJobs = 0;
        m_maxParseJobs = 0;
        m_neededPriority = BackgroundParser::WorstPriority;

        ThreadWeaver::setDebugLevel(true, 1);

        QObject::connect(&m_timer, SIGNAL(timeout()), m_parser, SLOT(parseDocuments()));

        loadSettings(); // Start the weaver
    }
    ~BackgroundParserPrivate()
    {
        suspend();

        m_weaver.dequeue();
        m_weaver.requestAbort();
        m_weaver.finish();

        // Release dequeued jobs
        QHashIterator<KUrl, ParseJob*> it = m_parseJobs;
        while (it.hasNext()) {
            it.next();
            it.value()->setBackgroundParser(0);
            delete it.value();
        }

        qDeleteAll(m_delayedParseJobs.values());
    }

    // Non-mutex guarded functions, only call with m_mutex acquired.
    void parseDocumentsInternal()
    {
        kDebug(9505) << "BackgroundParser::parseDocumentsInternal";
        
        // Create delayed jobs, that is, jobs for documents which have been changed
        // by the user.
        QList<ParseJob*> jobs;
        QHashIterator<KUrl, DocumentChangeTracker*> it = m_delayedParseJobs;
        while (it.hasNext()) {
            ParseJob* job = createParseJob(it.next().key(), TopDUContext::AllDeclarationsContextsAndUses, QList<QPointer<QObject> >());
            if (job) {
                job->setChangedRanges(it.value()->changedRanges());
                jobs.append(job);
            } else {
                kWarning() << "No job created for url " << it.key();
            }
        }
        qDeleteAll(m_delayedParseJobs);
        m_delayedParseJobs.clear();

        for (QMap<int, QSet<KUrl> >::Iterator it1 = m_documentsForPriority.begin();
             it1 != m_documentsForPriority.end(); ++it1 )
        {
            if(it1.key() > m_neededPriority)
                break; //The priority is not good enough to be processed right now
            
            for(QSet<KUrl>::Iterator it = it1.value().begin(); it != it1.value().end();) {
                //Only create parse-jobs for up to thread-count * 2 documents, so we don't fill the memory unnecessarily
                if(this->m_parseJobs.size() + jobs.size() > (m_threads*2)+1)
                    break;
                
                // When a document is scheduled for parsing while it is being parsed, it will be parsed
                // again once the job finished, but not now.
                if (m_parseJobs.contains(*it) ) {
                    ++it;
                    continue;
                }

                kDebug(9505) << "creating parse-job" << *it;
                ParseJob* job = createParseJob(*it, m_documents[*it].features, m_documents[*it].notifyWhenReady);
                if(job)
                    jobs.append(job);
                
                m_documents.remove(*it);
                it = it1.value().erase(it);
                --m_maxParseJobs; //We have added one when putting the document into m_documents
            }
        }

        // Ok, enqueueing is fine because m_parseJobs contains all of the jobs now

        foreach (ParseJob* job, jobs)
            m_weaver.enqueue(job);

        m_parser->updateProgressBar();

        //We don't hide the progress-bar in updateProgressBar, so it doesn't permanently flash when a document is reparsed again and again.
        if(m_doneParseJobs == m_maxParseJobs)
            emit m_parser->hideProgress();
    }

    ParseJob* createParseJob(const KUrl& url, TopDUContext::Features features, QList<QPointer<QObject> > notifyWhenReady)
    {
        QList<ILanguage*> languages = m_languageController->languagesForUrl(url);
        foreach (ILanguage* language, languages) {
            ParseJob* job = language->languageSupport()->createParseJob(url);
            if (!job) {
                continue; // Language part did not produce a valid ParseJob.
            }

            job->setMinimumFeatures(features);
            job->setBackgroundParser(m_parser);
            job->setNotifyWhenReady(notifyWhenReady);

            QObject::connect(job, SIGNAL(done(ThreadWeaver::Job*)),
                                m_parser, SLOT(parseComplete(ThreadWeaver::Job*)));
            QObject::connect(job, SIGNAL(failed(ThreadWeaver::Job*)),
                                m_parser, SLOT(parseComplete(ThreadWeaver::Job*)));
            QObject::connect(job, SIGNAL(progress(KDevelop::ParseJob*, float, QString)),
                                m_parser, SLOT(parseProgress(KDevelop::ParseJob*, float, QString)), Qt::QueuedConnection);

            m_parseJobs.insert(url, job);

            ++m_maxParseJobs;

            // TODO more thinking required here to support multiple parse jobs per url (where multiple language plugins want to parse)
            return job;
        }

        return 0;
    }


    void loadSettings()
    {
        KConfigGroup config(KGlobal::config(), "Background Parser");

        m_delay = config.readEntry("Delay", 500);
        m_timer.setInterval(m_delay);
        m_threads = config.readEntry("Real Number of Threads", 1);
        m_weaver.setMaximumNumberOfThreads(m_threads);

        if (config.readEntry("Enabled", true)) {
            resume();
        } else {
            suspend();
        }
    }

    void suspend()
    {
        bool s = m_weaver.state().stateId() == ThreadWeaver::Suspended ||
                 m_weaver.state().stateId() == ThreadWeaver::Suspending;

        if (s) { // Already suspending
            return;
        }

        m_timer.stop();
        m_weaver.suspend();
    }

    void resume()
    {
        bool s = m_weaver.state().stateId() == ThreadWeaver::Suspended ||
                 m_weaver.state().stateId() == ThreadWeaver::Suspending;

        if (m_timer.isActive() && !s) { // Not suspending
            return;
        }

        m_timer.start(m_delay);
        m_weaver.resume();
    }

    BackgroundParser *m_parser;
    ILanguageController* m_languageController;

    QTimer m_timer;
    int m_delay;
    int m_threads;

    struct DocumentParsePlan {
        DocumentParsePlan(int _priority = 0, TopDUContext::Features _features = TopDUContext::VisibleDeclarationsAndContexts) : priority(_priority), features(_features) {
        }
        int priority;
        TopDUContext::Features features;
        QList<QPointer<QObject> > notifyWhenReady;
    };
    // A list of known documents, and their priority
    QMap<KUrl, DocumentParsePlan > m_documents;
    QMap<int, QSet<KUrl> > m_documentsForPriority;
    // Current parse jobs
    QHash<KUrl, ParseJob*> m_parseJobs;
    QHash<KUrl, DocumentChangeTracker*> m_delayedParseJobs;

    QHash<KTextEditor::SmartRange*, KUrl> m_managedRanges;

    ThreadWeaver::Weaver m_weaver;
    ParserDependencyPolicy m_dependencyPolicy;

    QMutex m_mutex;

    int m_maxParseJobs;
    int m_doneParseJobs;
    QMap<KDevelop::ParseJob*, float> m_jobProgress;
    int m_neededPriority; //The minimum priority needed for processed jobs
};


BackgroundParser::BackgroundParser(ILanguageController *languageController)
    : QObject(languageController), d(new BackgroundParserPrivate(this, languageController))
{
}

BackgroundParser::~BackgroundParser()
{
    delete d;
}

QString BackgroundParser::statusName() const
{
    return i18n("Background Parser");
}

void BackgroundParser::clear(QObject* parent)
{
    QMutexLocker lock(&d->m_mutex);

    QHashIterator<KUrl, ParseJob*> it = d->m_parseJobs;
    while (it.hasNext()) {
        it.next();
        if (it.value()->parent() == parent) {
            it.value()->requestAbort();
        }
    }
}

void BackgroundParser::loadSettings(bool projectIsLoaded)
{
    Q_UNUSED(projectIsLoaded)

    d->loadSettings();
}

void BackgroundParser::saveSettings(bool projectIsLoaded)
{
    Q_UNUSED(projectIsLoaded)
}

void BackgroundParser::parseProgress(KDevelop::ParseJob* job, float value, QString text)
{
    Q_UNUSED(text)
    d->m_jobProgress[job] = value;
    updateProgressBar();
}

// void BackgroundParser::addUpdateJob(ReferencedTopDUContext topContext, TopDUContext::Features features, QObject* notifyWhenReady, int priority)
// {
//     QMutexLocker lock(&d->m_mutex);
//     {
//     }
// }

void BackgroundParser::addDocument(const KUrl& url, TopDUContext::Features features, int priority, QObject* notifyWhenReady)
{
//     kDebug(9505) << "BackgroundParser::addDocument" << url.prettyUrl();
    QMutexLocker lock(&d->m_mutex);
    {
        Q_ASSERT(url.isValid());
        
        BackgroundParserPrivate::DocumentParsePlan plan(priority, features);
        plan.notifyWhenReady += notifyWhenReady;
        
        QMap<KUrl, BackgroundParserPrivate::DocumentParsePlan >::const_iterator it = d->m_documents.find(url);
        
        if (it != d->m_documents.end()) {
            //Update the stored priority
            //Only allow upgrading
            
            if(it.value().priority < plan.priority)
                plan.priority = it.value().priority; //The stored priority is better, use that one
            
            if(it.value().features > plan.features)
                plan.features = it.value().features; //If the stored features are better, use those
            
            plan.notifyWhenReady +=  it.value().notifyWhenReady;
            
            //Update features + priority
            d->m_documentsForPriority[it.value().priority].remove(url);
            d->m_documents[url] = plan;
            d->m_documentsForPriority[priority].insert(url);
        }
        
        if (it == d->m_documents.end()) {
//             kDebug(9505) << "BackgroundParser::addDocument: queuing" << url;
            d->m_documents[url] = plan;
            d->m_documentsForPriority[priority].insert(url);
            ++d->m_maxParseJobs; //So the progress-bar waits for this document
        } else {
//             kDebug(9505) << "BackgroundParser::addDocument: is already queued:" << url;
        }

        if (!d->m_timer.isActive()) {
            d->m_timer.start();
        }
    }
}

void BackgroundParser::addDocumentList(const KUrl::List &urls, TopDUContext::Features features, int priority)
{
    foreach (KUrl url, urls)
        addDocument(url, features, priority);
}

void BackgroundParser::removeDocument(const KUrl &url)
{
    QMutexLocker lock(&d->m_mutex);

    Q_ASSERT(url.isValid());

    if(d->m_documents.contains(url)) {
        d->m_documentsForPriority[d->m_documents[url].priority].remove(url);
        d->m_documents.remove(url);
        --d->m_maxParseJobs;
    }
}

void BackgroundParser::parseDocuments()
{
    QMutexLocker lock(&d->m_mutex);

    d->parseDocumentsInternal();
}

void BackgroundParser::parseComplete(ThreadWeaver::Job* job)
{
    QMutexLocker lock(&d->m_mutex);

    if (ParseJob* parseJob = qobject_cast<ParseJob*>(job)) {
        kDebug(9505) << "BackgroundParser: parsed" << parseJob->document().str();

        emit parseJobFinished(parseJob);

        d->m_parseJobs.remove(parseJob->document().str());

        d->m_jobProgress.remove(parseJob);
        
        parseJob->setBackgroundParser(0);

		kDebug() << "Queueing job for deletion" << job->metaObject()->className() << "in thread" << QThread::currentThread();

		delete parseJob;

        ++d->m_doneParseJobs;
        updateProgressBar();

        //Continue creating more parse-jobs
        QMetaObject::invokeMethod(this, "parseDocuments", Qt::QueuedConnection);
    }
}

void BackgroundParser::disableProcessing()
{
    setNeededPriority(BestPriority);
}

void BackgroundParser::enableProcessing()
{
    setNeededPriority(WorstPriority);
}

void BackgroundParser::setNeededPriority(int priority)
{
    QMutexLocker lock(&d->m_mutex);
    d->m_neededPriority = priority;
    if(!d->m_timer.isActive())
        d->m_timer.start();
}

void BackgroundParser::suspend()
{
    d->suspend();

    emit hideProgress();
}

void BackgroundParser::resume()
{
    d->resume();

    updateProgressBar();
}

void BackgroundParser::updateProgressBar()
{
    if (d->m_doneParseJobs == d->m_maxParseJobs) {
        d->m_doneParseJobs = 0;
        d->m_maxParseJobs = 0;
    } else {
        float additionalProgress = 0;
        for(QMap<KDevelop::ParseJob*, float>::const_iterator it = d->m_jobProgress.begin(); it != d->m_jobProgress.end(); ++it)
            additionalProgress += *it;
        
        emit showProgress(0, d->m_maxParseJobs*1000, (additionalProgress + d->m_doneParseJobs)*1000);
    }
}

ParserDependencyPolicy* BackgroundParser::dependencyPolicy() const
{
    return &d->m_dependencyPolicy;
}

ParseJob* BackgroundParser::parseJobForDocument(const KUrl& document) const
{
    QMutexLocker lock(&d->m_mutex);

    if (d->m_parseJobs.contains(document)) {
        return d->m_parseJobs[document];
    }

    return 0;
}

void BackgroundParser::setThreadCount(int threadCount)
{
    if (d->m_threads != threadCount) {
        d->m_threads = threadCount;
        d->m_weaver.setMaximumNumberOfThreads(d->m_threads);
    }
}

void BackgroundParser::setDelay(int miliseconds)
{
    if (d->m_delay != miliseconds) {
        d->m_delay = miliseconds;
        d->m_timer.setInterval(d->m_delay);
    }
}

void BackgroundParser::addManagedTopRange(const KUrl& document, KTextEditor::SmartRange* range)
{
    range->addWatcher(this);
    d->m_managedRanges.insert(range, document);
}

void BackgroundParser::removeManagedTopRange(KTextEditor::SmartRange* range)
{
    range->removeWatcher(this);
    d->m_managedRanges.remove(range);
}

void BackgroundParser::rangeContentsChanged(KTextEditor::SmartRange* range, KTextEditor::SmartRange* mostSpecificChild)
{
    QMutexLocker l(&d->m_mutex);

    // Smart mutex is already locked
    KUrl documentUrl = range->document()->url();

    if (d->m_parseJobs.contains(documentUrl)) {
        ParseJob* job = d->m_parseJobs[documentUrl];
        if (job->addChangedRange( mostSpecificChild ))
            // Success
            return;
    }

    // Initially I just created a new parse job here, but that causes a deadlock as the smart mutex is locked
    // So store the info in a class with just the changed ranges information...
    DocumentChangeTracker* newTracker = 0;
    if (d->m_delayedParseJobs.contains(documentUrl))
        newTracker = d->m_delayedParseJobs[documentUrl];

    if (!newTracker) {
        newTracker = new DocumentChangeTracker();
        d->m_delayedParseJobs.insert(documentUrl, newTracker);
    }

    newTracker->addChangedRange( mostSpecificChild );
    
    if (!d->m_timer.isActive())
        d->m_timer.start(d->m_delay);
}

}

#include "backgroundparser.moc"

