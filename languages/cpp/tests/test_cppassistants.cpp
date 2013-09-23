/* This file is part of KDevelop
   Copyright 2012 Olivier de Gaalon <olivier.jg@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "test_cppassistants.h"

#include "../codegen/renameassistant.h"
#include "../codegen/renameaction.h"
#include "../codegen/codeassistant.h"

#include <QtTest/QtTest>
#include <KTempDir>
#include <KAction>
#include <qtest_kde.h>

#include <tests/autotestshell.h>
#include <tests/testcore.h>
#include <interfaces/foregroundlock.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/isourceformattercontroller.h>
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <language/backgroundparser/backgroundparser.h>
#include <language/duchain/duchain.h>
#include <language/codegen/coderepresentation.h>
#include <shell/documentcontroller.h>
#include <interfaces/ilanguage.h>
#include <navigation/macronavigationcontext.h>
#include <navigation/navigationwidget.h>

using namespace KDevelop;
using namespace KTextEditor;

QTEST_KDEMAIN(TestCppAssistants, GUI)

ForegroundLock *globalTestLock = 0;
Cpp::StaticCodeAssistant *staticCodeAssistant = 0;

void TestCppAssistants::initTestCase()
{
  AutoTestShell::init(QStringList() << "kdevcppsupport");
  TestCore::initialize();
  DUChain::self()->disablePersistentStorage();
  Core::self()->languageController()->backgroundParser()->setDelay(0);
  Core::self()->sourceFormatterController()->disableSourceFormatting(true);
  CodeRepresentation::setDiskChangesForbidden(true);

  staticCodeAssistant = new Cpp::StaticCodeAssistant();
  globalTestLock = new ForegroundLock;
}

void TestCppAssistants::cleanupTestCase()
{
  delete staticCodeAssistant;
  Core::self()->cleanup();
  delete globalTestLock;
  globalTestLock = 0;
}

QString createFile(QString fileContents)
{
  static KTempDir dirA;
  static int i = 0;
  ++i;
  QFile file(dirA.name() + QString::number(i) + ".cpp");
  file.open(QIODevice::WriteOnly | QIODevice::Text);
  file.write(fileContents.toUtf8());
  file.close();
  return file.fileName();
}

class Testbed
{
public:
  enum TestDoc
  {
    HeaderDoc,
    CppDoc
  };

  Testbed(QString headerContents, QString cppContents)
  {
    m_headerDocument.url = createFile(headerContents);
    m_headerDocument.textDoc = openDocument(m_headerDocument.url);

    m_cppDocument.url = createFile(cppContents.prepend(QString("#include \"%1\"\n").arg(m_headerDocument.url)));
    m_cppDocument.textDoc = openDocument(m_cppDocument.url);
  }
  ~Testbed()
  {
    Core::self()->documentController()->documentForUrl(m_cppDocument.url)->close(KDevelop::IDocument::Discard);
    Core::self()->documentController()->documentForUrl(m_headerDocument.url)->close(KDevelop::IDocument::Discard);
  }

  void changeDocument(TestDoc which, Range where, QString what, bool waitForUpdate = false)
  {
    TestDocument document;
    if (which == CppDoc)
    {
      document = m_cppDocument;
      where = Range(where.start().line() + 1, where.start().column(),
                    where.end().line() + 1, where.end().column()); //The include adds a line
    }
    else
      document = m_headerDocument;
    document.textDoc->activeView()->setSelection(where);
    document.textDoc->activeView()->removeSelectionText();
    document.textDoc->activeView()->setCursorPosition(where.start());
    document.textDoc->activeView()->insertText(what);
    QCoreApplication::processEvents();
    if (waitForUpdate)
      DUChain::self()->waitForUpdate(IndexedString(document.url), KDevelop::TopDUContext::AllDeclarationsAndContexts);
  }

  QString documentText(TestDoc which)
  {
    if (which == CppDoc)
    {
      //The CPP document text shouldn't include the autogenerated include line
      QString text = m_cppDocument.textDoc->text();
      return text.mid(text.indexOf("\n") + 1);
    }
    else
      return m_headerDocument.textDoc->text();
  }
private:
  struct TestDocument {
    QString url;
    Document *textDoc;
  };

  Document* openDocument(QString url)
  {
    Core::self()->documentController()->openDocument(url);
    DUChain::self()->waitForUpdate(IndexedString(url), KDevelop::TopDUContext::AllDeclarationsAndContexts);
    return Core::self()->documentController()->documentForUrl(url)->textDocument();
  }

  TestDocument m_headerDocument;
  TestDocument m_cppDocument;
};


/**
 * A StateChange describes an insertion/deletion/replacement and the expected result
**/
struct StateChange
{
  StateChange(){};
  StateChange(Testbed::TestDoc _document, Range _range, QString _newText, QString _result)
  {
    document = _document;
    range = _range;
    newText = _newText;
    result = _result;
  }
  Testbed::TestDoc document;
  Range range;
  QString newText;
  QString result;
};

Q_DECLARE_METATYPE(StateChange)
Q_DECLARE_METATYPE(QList<StateChange>)

void TestCppAssistants::testRenameAssistant_data()
{
  QTest::addColumn<QString>("fileContents");
  QTest::addColumn<QString>("oldDeclarationName");
  QTest::addColumn<QList<StateChange> >("stateChanges");
  QTest::addColumn<QString>("finalFileContents");

  QTest::newRow("Prepend Text")
    << "int foo(int i)\n { i = 0; return i; }"
    << "i"
    << (QList<StateChange>() << StateChange(Testbed::CppDoc, Range(0,12,0,12), "u", "ui"))
    << "int foo(int ui)\n { ui = 0; return ui; }";

  QTest::newRow("Append Text")
    << "int foo(int i)\n { i = 0; return i; }"
    << "i"
    << (QList<StateChange>() << StateChange(Testbed::CppDoc, Range(0,13,0,13), "d", "id"))
    << "int foo(int id)\n { id = 0; return id; }";

  QTest::newRow("Replace Text")
    << "int foo(int i)\n { i = 0; return i; }"
    << "i"
    << (QList<StateChange>() << StateChange(Testbed::CppDoc, Range(0,12,0,13), "u", "u"))
    << "int foo(int u)\n { u = 0; return u; }";

  QTest::newRow("Letter-by-Letter")
    << "int foo(int i)\n { i = 0; return i; }"
    << "i"
    << (QList<StateChange>()
         << StateChange(Testbed::CppDoc, Range(0,12,0,13), "", "")
         << StateChange(Testbed::CppDoc, Range(0,12,0,12), "a", "a")
         << StateChange(Testbed::CppDoc, Range(0,13,0,13), "b", "ab")
         << StateChange(Testbed::CppDoc, Range(0,14,0,14), "c", "abc")
       )
    << "int foo(int abc)\n { abc = 0; return abc; }";

  QTest::newRow("Paste Replace")
    << "int foo(int abg)\n { abg = 0; return abg; }"
    << "abg"
    << (QList<StateChange>() << StateChange(Testbed::CppDoc, Range(0,12,0,15), "abcdefg", "abcdefg"))
    << "int foo(int abcdefg)\n { abcdefg = 0; return abcdefg; }";

  QTest::newRow("Paste Insert")
    << "int foo(int abg)\n { abg = 0; return abg; }"
    << "abg"
    << (QList<StateChange>() << StateChange(Testbed::CppDoc, Range(0,14,0,14), "cdef", "abcdefg"))
    << "int foo(int abcdefg)\n { abcdefg = 0; return abcdefg; }";

  QTest::newRow("Letter-by-Letter Insert")
    << "int foo(int abg)\n { abg = 0; return abg; }"
    << "abg"
    << (QList<StateChange>()
         << StateChange(Testbed::CppDoc, Range(0,14,0,14), "c", "abcg")
         << StateChange(Testbed::CppDoc, Range(0,15,0,15), "d", "abcdg")
         << StateChange(Testbed::CppDoc, Range(0,16,0,16), "e", "abcdeg")
         << StateChange(Testbed::CppDoc, Range(0,17,0,17), "f", "abcdefg")
       )
    << "int foo(int abcdefg)\n { abcdefg = 0; return abcdefg; }";
}

void TestCppAssistants::testRenameAssistant()
{
  QFETCH(QString, fileContents);
  Testbed testbed("", fileContents);

  QFETCH(QString, oldDeclarationName);
  QFETCH(QList<StateChange>, stateChanges);
  foreach(StateChange stateChange, stateChanges)
  {
    testbed.changeDocument(Testbed::CppDoc, stateChange.range, stateChange.newText);
    if (stateChange.result.isEmpty())
    {
      QVERIFY(!staticCodeAssistant->activeAssistant() || !staticCodeAssistant->activeAssistant()->actions().size());
    }
    else
    {
      QVERIFY(staticCodeAssistant->activeAssistant() && staticCodeAssistant->activeAssistant()->actions().size());
      Cpp::RenameAction *r = dynamic_cast<Cpp::RenameAction*>(staticCodeAssistant->activeAssistant()->actions().first().data());
      QCOMPARE(r->oldDeclarationName(), oldDeclarationName);
      QCOMPARE(r->newDeclarationName(), stateChange.result);
    }
  }
  if (staticCodeAssistant->activeAssistant() && staticCodeAssistant->activeAssistant()->actions().size())
    staticCodeAssistant->activeAssistant()->actions().first()->execute();
  QFETCH(QString, finalFileContents);
  QCOMPARE(testbed.documentText(Testbed::CppDoc), finalFileContents);
}

const QString SHOULD_ASSIST = "SHOULD_ASSIST"; //An assistant will be visible
const QString NO_ASSIST = "NO_ASSIST";         //No assistant visible
void TestCppAssistants::testSignatureAssistant_data()
{
  QTest::addColumn<QString>("headerContents");
  QTest::addColumn<QString>("cppContents");
  QTest::addColumn<QList<StateChange> >("stateChanges");
  QTest::addColumn<QString>("finalHeaderContents");
  QTest::addColumn<QString>("finalCppContents");

  QTest::newRow("Change Argument Type")
    << "class Foo {\nint bar(int a, char* b, int c = 10); \n};"
    << "int Foo::bar(int a, char* b, int c)\n{ a = c; b = new char; return a + *b; }"
    << (QList<StateChange>() << StateChange(Testbed::HeaderDoc, Range(1,8,1,11), "char", SHOULD_ASSIST))
    << "class Foo {\nint bar(char a, char* b, int c = 10); \n};"
    << "int Foo::bar(char a, char* b, int c)\n{ a = c; b = new char; return a + *b; }";

  QTest::newRow("Change Default Parameter")
    << "class Foo {\nint bar(int a, char* b, int c = 10); \n};"
    << "int Foo::bar(int a, char* b, int c)\n{ a = c; b = new char; return a + *b; }"
    << (QList<StateChange>() << StateChange(Testbed::HeaderDoc, Range(1,29,1,34), "", NO_ASSIST))
    << "class Foo {\nint bar(int a, char* b, int c); \n};"
    << "int Foo::bar(int a, char* b, int c)\n{ a = c; b = new char; return a + *b; }";

  QTest::newRow("Change Function Type")
    << "class Foo {\nint bar(int a, char* b, int c = 10); \n};"
    << "int Foo::bar(int a, char* b, int c)\n{ a = c; b = new char; return a + *b; }"
    << (QList<StateChange>() << StateChange(Testbed::CppDoc, Range(0,0,0,3), "char", SHOULD_ASSIST))
    << "class Foo {\nchar bar(int a, char* b, int c = 10); \n};"
    << "char Foo::bar(int a, char* b, int c)\n{ a = c; b = new char; return a + *b; }";

  //Interesting corner case: trying to do this in only one change will fail, because the declarationBuilder
  //reuses the declarations and doesn't reorganize the parameters internally (though it does change the ranges)
  //This could be fixed by making the signature assistant reorganize according to range... if anyone cares.
  QTest::newRow("Swap Args Definition Side")
    << "class Foo {\nint bar(int a, char* b, int c = 10); \n};"
    << "int Foo::bar(int a, char* b, int c)\n{ a = c; b = new char; return a + *b; }"
    << (QList<StateChange>()
         << StateChange(Testbed::CppDoc, Range(0,13,0,28), "char* b, ", SHOULD_ASSIST)
         << StateChange(Testbed::CppDoc, Range(0,22,0,22), "int a,", SHOULD_ASSIST)
       )
    << "class Foo {\nint bar(char* b, int a, int c = 10); \n};"
    << "int Foo::bar(char* b, int a, int c)\n{ a = c; b = new char; return a + *b; }";

  // see https://bugs.kde.org/show_bug.cgi?id=299393
  // actually related to the whitespaces in the header...
  QTest::newRow("Change Function Constness")
    << "class Foo {\nvoid bar( const Foo& ) const;\n};"
    << "void Foo::bar(const Foo&) const\n{}"
    << (QList<StateChange>() << StateChange(Testbed::CppDoc, Range(0,25,0,31), "", SHOULD_ASSIST))
    << "class Foo {\nvoid bar( const Foo& );\n};"
    << "void Foo::bar(const Foo&)\n{}";
}

void TestCppAssistants::testSignatureAssistant()
{
  QFETCH(QString, headerContents);
  QFETCH(QString, cppContents);
  Testbed testbed(headerContents, cppContents);

  QFETCH(QList<StateChange>, stateChanges);
  foreach(StateChange stateChange, stateChanges)
  {
    testbed.changeDocument(stateChange.document, stateChange.range, stateChange.newText, true);

    if (stateChange.result == SHOULD_ASSIST)
      QVERIFY(staticCodeAssistant->activeAssistant() && staticCodeAssistant->activeAssistant()->actions().size());
    else
      QVERIFY(!staticCodeAssistant->activeAssistant() || !staticCodeAssistant->activeAssistant()->actions().size());
  }
  if (staticCodeAssistant->activeAssistant() && staticCodeAssistant->activeAssistant()->actions().size())
    staticCodeAssistant->activeAssistant()->actions().first()->execute();

  QFETCH(QString, finalHeaderContents);
  QFETCH(QString, finalCppContents);
  QCOMPARE(testbed.documentText(Testbed::HeaderDoc), finalHeaderContents);
  QCOMPARE(testbed.documentText(Testbed::CppDoc), finalCppContents);
}

void TestCppAssistants::testMacroExpansion_data()
{
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");
  QTest::addColumn<int>("macroLine");

  QTest::newRow("nonewlines") << "#define FOO(arg1,arg2,arg3) BAR(arg1,arg2,arg3)\nint main(){\nFOO(1,2,3)\n}"
                              << "BAR(1 ,2 ,3 ) "
                              << 2;

  QTest::newRow("newlines") << "#define FOO(arg1,arg2,arg3) BAR(arg1,arg2,arg3)\nint main(){\nFOO(1,\n2,\n3)\n}"
                            << "BAR(1 ,2 ,3 ) "
                            << 2;

  QTest::newRow("invalid")  << "#define FOO(arg1,arg2,arg3) BAR(arg1,arg2,arg3)\nint main(){\nFOO(1,\n2,\n3\n}"
                            << "FOO"
                            << 2;

  QTest::newRow("multibrace") << "#define FOO(arg1,arg2,arg3) BAR(arg1,arg2,arg3)\nint main(){\nFOO((1),\n2,\n3)\n}"
                              << "BAR((1) ,2 ,3 ) "
                              << 2;
}

void TestCppAssistants::testMacroExpansion()
{
  QFETCH(QString, input);
  QFETCH(QString, expected);
  QFETCH(int, macroLine);

  KUrl url(createFile(input));
  Core::self()->documentController()->openDocument(url);
  DUChain::self()->waitForUpdate(IndexedString(url), KDevelop::TopDUContext::AllDeclarationsAndContexts);

  ILanguage* language = ICore::self()->languageController()->languagesForUrl(url).at(0);
  QVERIFY(language);

  QWidget *macroWidget = language->languageSupport()->specialLanguageObjectNavigationWidget(url, SimpleCursor(macroLine,0));
  QVERIFY(macroWidget);
  Cpp::NavigationWidget *macroNavigationWidget = dynamic_cast<Cpp::NavigationWidget*>(macroWidget);
  QVERIFY(macroNavigationWidget);
  Cpp::MacroNavigationContext *macroContext = dynamic_cast<Cpp::MacroNavigationContext*>(macroNavigationWidget->context().data());
  QVERIFY(macroContext);

  QCOMPARE(macroContext->body(),expected);
  Core::self()->documentController()->documentForUrl(url)->close(KDevelop::IDocument::Discard);
}

#include "test_cppassistants.moc"
