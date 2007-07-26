/* This file is part of KDevelop
    Copyright 2002-2005 Roberto Raggi <roberto@kdevelop.org>

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

#include "ast.h"

CommentAST::CommentAST ( const CommentAST& rhs )
{}
CommentAST& CommentAST::operator= ( const CommentAST& rhs )
{
  return *this;
}
CommentAST::CommentAST() : m_comment ( 0 )
{}

void CommentAST::setComment ( const QString& comment )
{
  if ( !m_comment )
    m_comment = new QString;
  *m_comment = comment;
}

void CommentAST::addComment ( const QString& comment )
{
  if ( comment.isEmpty() )
    return;

  if ( !m_comment )
    m_comment = new QString;

  if ( !m_comment->isEmpty() )
  {
    *m_comment += "\n(" + comment + ")";
  }
  else
  {
    *m_comment = comment;
  }
}

bool CommentAST::haveComment() const
{
  return m_comment && !m_comment->isEmpty();
}
CommentAST::~CommentAST()
{
  ///@todo make sure this is executed!
  delete m_comment;
}

// kate: space-indent on; indent-width 2; replace-tabs on;
