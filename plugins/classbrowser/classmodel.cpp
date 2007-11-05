/*
 * KDevelop Class Browser
 *
 * Copyright 2007 Hamish Rodda <rodda@kde.org>
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

#include "classmodel.h"

#include <klocale.h>
#include <kmessagebox.h>
#include <ktemporaryfile.h>
#include <kprocess.h>

#include "idocument.h"
#include "icore.h"
#include "ilanguagecontroller.h"
#include "backgroundparser/backgroundparser.h"
#include "backgroundparser/parsejob.h"

#include "classbrowserpart.h"
#include "topducontext.h"
#include "declaration.h"
#include "definition.h"
#include "parsingenvironment.h"
#include "duchain.h"
#include "duchainlock.h"
#include "duchainutils.h"

//#include "modeltest.h"

using namespace KTextEditor;
using namespace KDevelop;

ClassModel::ClassModel(ClassBrowserPart* parent)
  : QAbstractItemModel(parent)
  , m_topList(0L)
  , m_filterDocument(0L)
{
  //new ModelTest(this);

  bool success = connect(DUChain::self()->notifier(), SIGNAL(branchAdded(KDevelop::DUContextPointer)), SLOT(branchAdded(KDevelop::DUContextPointer)), Qt::QueuedConnection);
  Q_ASSERT(success);
}

ClassBrowserPart* ClassModel::part() const {
  return qobject_cast<ClassBrowserPart*>(QObject::parent());
}

ClassModel::~ClassModel()
{
  delete m_topList;
  qDeleteAll(m_lists);
  qDeleteAll(m_knownObjects);
}

void ClassModel::resetModel()
{
  delete m_topList;
  m_topList = 0L;
  qDeleteAll(m_lists);
  m_lists.clear();
  qDeleteAll(m_knownObjects);
  m_knownObjects.clear();
  m_namespaces.clear();

  reset();
}

void ClassModel::setFilterDocument(KDevelop::IDocument* document)
{
  if (m_filterDocument != document) {
    m_filterDocument = document;
    resetModel();
  }
}

int ClassModel::columnCount(const QModelIndex & parent) const
{
  Q_UNUSED(parent);

  return 1;
}

ClassModel::Node* ClassModel::objectForIndex(const QModelIndex& index) const
{
  return static_cast<Node*>(index.internalPointer());
}

QModelIndex ClassModel::index(int row, int column, const QModelIndex & parentIndex) const
{
  if (row < 0 || column < 0 || column > 0)
    return QModelIndex();

  DUChainReadLocker readLock(DUChain::lock());

  QList<Node*>* children;
  if (!parentIndex.isValid()) {
    children = childItems(0L);

  } else {
    Node* parent = objectForIndex(parentIndex);
    children = childItems(parent);
  }

  if (!children)
    return QModelIndex();

  if (row < children->count())
    return createIndex(row, column, children->at(row));

  return QModelIndex();
}

bool ClassModel::hasChildren(const QModelIndex& parentIndex) const
{
  DUChainReadLocker readLock(DUChain::lock());

  Node* parent = objectForIndex(parentIndex);
  if (!parent)
    return childItems(parent);

  if (!*parent)
    return false;

  DUContext* context = dynamic_cast<DUContext*>(parent->data());
  if (!context)
    return false;

  if (!context->localDeclarations().isEmpty())
    return true;

  if (!context->localDefinitions().isEmpty())
    return true;

  if (context->childContexts().isEmpty())
    return false;

  return childItems(parent);
}

int ClassModel::rowCount(const QModelIndex & parentIndex) const
{
  DUChainReadLocker readLock(DUChain::lock());

  Node* parent = objectForIndex(parentIndex);
  if (parent && !*parent)
    return 0;

  QList<Node*>* children = childItems(parent);
  if (!children)
    return 0;

  return children->count();
}

QModelIndex ClassModel::parent(const QModelIndex & index) const
{
  if (!index.isValid())
    return QModelIndex();

  DUChainReadLocker readLock(DUChain::lock());

  Node* base = objectForIndex(index);
  if (!base)
    return QModelIndex();

  Node* parent = base->parent();

  if (!parent)
    return QModelIndex();

  Node* grandParent = parent->parent();

  QList<Node*>* list = childItems(grandParent);

  int i = list->indexOf(parent);
  Q_ASSERT(i != -1);

  return createIndex(i, 0, parent);
}

bool ClassModel::orderItems(ClassModel::Node* p1, ClassModel::Node* p2)
{
  QString s1, s2;

  if (DUContext* d = dynamic_cast<DUContext*>(p1->data())) {
    if (d->owner())
      if (Definition* definition = d->owner()->asDefinition()) {
        s1 = definition->declaration()->identifier().toString();

      } else if (Declaration* declaration = d->owner()->asDeclaration()) {
        s1 = declaration->identifier().toString();
      }

  } else if (Declaration* d = dynamic_cast<Declaration*>(p1->data())) {
    s1 = d->identifier().toString();

  } else if (Definition* d = dynamic_cast<Definition*>(p1->data())) {
    s1 = d->declaration()->identifier().toString();
  }

  if (DUContext* d = dynamic_cast<DUContext*>(p2->data())) {
    if (d->owner())
      if (Definition* definition = d->owner()->asDefinition()) {
        s2 = definition->declaration()->identifier().toString();

      } else if (Declaration* declaration = d->owner()->asDeclaration()) {
        s2 = declaration->identifier().toString();
      }
  } else if (Declaration* d = dynamic_cast<Declaration*>(p2->data())) {
    s2 = d->identifier().toString();

  } else if (Definition* d = dynamic_cast<Definition*>(p2->data())) {
    s2 = d->declaration()->identifier().toString();
  }

  return QString::localeAwareCompare(s1, s2);
}

QList<ClassModel::Node*>* ClassModel::childItems(Node* parent) const
{
  ENSURE_CHAIN_READ_LOCKED

  if (!parent) {
    if (m_topList)
      return m_topList;

  } else if (m_lists.contains(parent)) {
    return m_lists[parent];
  }

  QList<Node*>* list = new QList<Node*>();

  if (parent) {
    if (DUContext* parentContext = dynamic_cast<DUContext*>(parent->data()))
      addTopLevelToList(parentContext, list, parent);

    foreach (DUContextPointer nsContext, parent->namespaceContexts())
      addTopLevelToList(nsContext.data(), list, parent);

  } else {
    foreach (TopDUContext* chain, DUChain::self()->allChains())
      addTopLevelToList(chain, list, parent);
  }

  //qSort(list->begin(), list->end(), orderItems);

  if (!parent)
    m_topList = list;
  else
    m_lists.insert(parent, list);

  return list;
}

void ClassModel::addTopLevelToList(DUContext* context, QList<Node*>* list, Node* parent, bool first) const
{
  foreach (DUContext* child, context->childContexts()) {
    if (m_filterDocument && child->url() != m_filterDocument->url())
      continue;

    switch (child->type()) {
      case DUContext::Class:
        if (child->owner())
          list->append(createPointer(child, parent));
        break;

      case DUContext::Namespace: {
        Node* ns;
        if (m_namespaces.contains(child->scopeIdentifier())) {
          ns = m_namespaces[child->scopeIdentifier()];

          if (list->contains(ns))
            break;

        } else {
          ns = createPointer(child, parent);
        }

        // FIXME must emit changes here??
        ns->addNamespaceContext(DUContextPointer(child));

        if (!list->contains(ns))
          list->append(ns);

        break;
      }

      default:
        addTopLevelToList(child, list, parent, false);
        break;
    }
  }

  if (first) {
    foreach (Declaration* declaration, context->localDeclarations())
      if (!m_filterDocument || declaration->url() == m_filterDocument->url())
        list->append(createPointer(declaration, parent));

    foreach (Definition* definition, context->localDefinitions())
      if (!m_filterDocument || definition->url() == m_filterDocument->url())
        list->append(createPointer(definition, parent));
  }
}

DUContext* ClassModel::trueParent(DUContext* parent) const
{
  while (parent) {
    switch (parent->type()) {
      case DUContext::Class:
      case DUContext::Namespace:
        return parent;
      default:
        break;
    }

    parent = parent->parentContext();
  }

  return 0L;
}

void ClassModel::branchAdded(DUContextPointer context)
{
  DUChainReadLocker readLock(DUChain::lock());

  if (context)
    contextAdded(pointer(trueParent(context->parentContext())), context.data());
}

void ClassModel::contextAdded(Node* parent, DUContext* context)
{
  if (parent && !m_lists.contains(parent))
    // The parent node is not yet discovered, it will be figured out later if needed
    return;

  if (m_filterDocument && context->url() != m_filterDocument->url())
    return;

  if ((context->type() == DUContext::Class && context->owner()) || context->type() == DUContext::Namespace) {
    if (context->type() == DUContext::Namespace) {
      if (m_namespaces.contains(context->scopeIdentifier()))
        // This namespace is already known
        return;
    }

    QList<Node*>* list = childItems(parent);
    if (!list) {
      kWarning() << "Could not find list of child objects for" << parent;
      return;
    }

    Node* contextPointer = createPointer(context, parent);

    QList<Node*>::Iterator it = qUpperBound(list->begin(), list->end(), contextPointer, orderItems);

    int index = 0;
    if (it != list->end())
      index = list->indexOf(*it);

    beginInsertRows(QModelIndex(), index, index);
    list->insert(index, contextPointer);
    endInsertRows();

  } else {
    foreach (DUContext* child, context->childContexts())
      contextAdded(parent, child);
  }
}

ClassModel::Node* ClassModel::pointer(DUChainBase* object) const
{
  Node* ret = 0L;

  if (object &&m_knownObjects.contains(object))
    ret = m_knownObjects[object];

  return ret;
}

ClassModel::Node* ClassModel::createPointer(DUContext* context, Node* parent) const
{
  if (context->type() == DUContext::Namespace) {
    Q_ASSERT(!m_namespaces.contains(context->scopeIdentifier()));

    Node* n = createPointer(static_cast<DUChainBase*>(context), parent);
    m_namespaces.insert(context->scopeIdentifier(), n);

    return n;
  }

  return createPointer(static_cast<DUChainBase*>(context), parent);
}

ClassModel::Node* ClassModel::createPointer(DUChainBase* object, Node* parent) const
{
  Node* ret;

  if (!m_knownObjects.contains(object)) {
    ret = new Node(object, parent);
    m_knownObjects.insert(object, ret);

  } else {
    ret = m_knownObjects[object];
  }

  /*if (dynamic_cast<DUContext*>(object))
    kDebug() << k_funcinfo << "Context" << object << ret;

  else if (dynamic_cast<Declaration*>(object))
    kDebug() << k_funcinfo << "Declaration" << object << ret;

  else if (dynamic_cast<Definition*>(object))
    kDebug() << k_funcinfo << "Definition" << object << ret;*/

  return ret;
}

QVariant ClassModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid())
    return QVariant();

  DUChainReadLocker readLock(DUChain::lock());

  Node* basep = objectForIndex(index);
  if (!basep)
    return QVariant();

  DUChainBase* base = basep->data();
  if (!base)
    return QVariant();

  if (DUContext* context = dynamic_cast<DUContext*>(base)) {
    switch (context->type()) {
      case DUContext::Class:
        if (context->owner()) {
          if (Definition* definition = context->owner()->asDefinition()) {
            switch (role) {
              case Qt::DisplayRole:
                return definition->declaration()->identifier().toString();
              case Qt::DecorationRole:
                return DUChainUtils::iconForDeclaration(definition->declaration());
            }

          } else if (Declaration* declaration = context->owner()->asDeclaration()) {
            switch (role) {
              case Qt::DisplayRole:
                return declaration->identifier().toString();
              case Qt::DecorationRole:
                return DUChainUtils::iconForDeclaration(declaration);
            }
          }
        }

      case DUContext::Namespace:
        switch (role) {
          case Qt::DisplayRole:
            return context->localScopeIdentifier().toString();
          case Qt::DecorationRole:
            return KIcon("namespace");
        }
    }

  } else if (Declaration* dec = dynamic_cast<Declaration*>(base)) {
    switch (role) {
      case Qt::DisplayRole: {
        QString ret = dec->identifier().toString();
        if (FunctionType::Ptr type = dec->type<FunctionType>())
          ret += type->toString(FunctionType::SignatureArguments);
        return ret;
      }
      case Qt::DecorationRole:
        return DUChainUtils::iconForDeclaration(dec);
    }

  } else if (Definition* def = dynamic_cast<Definition*>(base)) {
    switch (role) {
      case Qt::DisplayRole:
        if (def->declaration())
          return def->declaration()->identifier().toString();
        else
          return i18n("<No declaration for definition>");
      case Qt::DecorationRole:
        return DUChainUtils::iconForDeclaration(def->declaration());
    }

  } else {
    switch (role) {
      case Qt::DisplayRole:
        return i18n("Unknown object!");
    }
  }

  return QVariant();
}

Declaration* ClassModel::declarationForObject(const DUChainBasePointer& pointer) const
{
  if (!pointer)
    return 0L;

  if (Declaration* declaration = dynamic_cast<Declaration*>(pointer.data())) {
    return declaration;

  } else if (Definition* d = dynamic_cast<Definition*>(pointer.data())) {
    return d->declaration();

  } else if (DUContext* context = dynamic_cast<DUContext*>(pointer.data())) {
    if (context->owner())
      if (Definition* definition = context->owner()->asDefinition())
        return definition->declaration();
      else
        return context->owner()->asDeclaration();
  }

  return 0L;
}

Definition* ClassModel::definitionForObject(const DUChainBasePointer& pointer) const
{
  if (!pointer)
    return 0L;

  if (Definition* d = dynamic_cast<Definition*>(pointer.data())) {
    return d;

  } else if (DUContext* context = dynamic_cast<DUContext*>(pointer.data())) {
    if (context->owner())
      return context->owner()->asDefinition();
  }

  return 0L;
}

#include "classmodel.moc"

// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on
