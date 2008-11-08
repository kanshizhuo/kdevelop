/*
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

#ifndef IMPLEMENTATIONHELPERITEM_H
#define IMPLEMENTATIONHELPERITEM_H

#include "completionitem.h"

using namespace KDevelop;

class ImplementationHelperItem : public NormalDeclarationCompletionItem
{
public:
  ImplementationHelperItem(KDevelop::DeclarationPointer decl = KDevelop::DeclarationPointer(), KSharedPtr<Cpp::CodeCompletionContext> context=KSharedPtr<Cpp::CodeCompletionContext>(), int _inheritanceDepth = 0, int _listOffset=0);
  
  virtual QVariant data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const;
  virtual void execute(KTextEditor::Document* document, const KTextEditor::Range& word);
};

#endif // IMPLEMENTATIONHELPERITEM_H
