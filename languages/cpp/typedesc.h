/***************************************************************************
   copyright            : (C) 2006 by David Nolden
   email                : david.nolden.kdevelop@art-master.de
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef __TYPEDESC_H__
#define __TYPEDESC_H__

#include <ktexteditor/codecompletioninterface.h>
#include <ksharedptr.h>


#include "typedecoration.h"

class TypeDescShared;
class SimpleTypeImpl;

typedef KSharedPtr<TypeDescShared> TypeDescPointer;
typedef KSharedPtr<SimpleTypeImpl> TypePointer;

class TypeDesc {
public:
 typedef QValueList<TypeDescPointer> TemplateParams;
 static const char* functionMark;

	///These flags have no internal use, they are set and read from the outside
	enum Flags {
		Standard = 0,
		ResolutionTried = 1  ///means that the resolution was tried, and should not be retried.
	};
private:
 QString m_cleanName;
 int m_pointerDepth;
 int m_functionDepth;
 TemplateParams m_templateParams;
 TypeDescPointer m_nextType;
 TypePointer m_resolved;
 TypeDecoration m_dec;
 Flags m_flags;

 
 void init( QString stri );
public:
	///clears the current template-parameters, and extracts those from the given string
 void takeTemplateParams( const QString& string );
 
 TypeDesc( const QString& name = "" );
 
 TypeDesc( const TypeDesc& rhs );
 
 bool deeper( const TypeDesc& rhs ) const {
  return depth() > rhs.depth();
 }
 
 bool longer( const TypeDesc& rhs ) const {
  return length() > rhs.length();
 }
 
 TypeDesc& operator = ( const TypeDesc& rhs );
 
 TypeDesc& operator = ( const QString& rhs ) {
  init( rhs );
  return *this;
 }

	TypeDesc firstType() const {
		TypeDesc ret = *this;
		ret.setNext( 0 );
		return ret;
	}
  
	///this function must be remade
 bool isValidType() const ;
 
 int depth() const;
 
 int length() const ;
 
 ///Something is wrong with this function.. so i use the string-comparison
 int compare ( const TypeDesc& rhs ) const;
 
 bool operator < ( const TypeDesc& rhs ) const {
  return compare( rhs ) == -1;
 }
 
 bool operator > ( const TypeDesc& rhs ) const {
  return compare( rhs ) == 1;
 }
 
 bool operator == ( const TypeDesc& rhs ) const {
  return compare( rhs ) == 0;
 }
 
 QString nameWithParams() const;
 
	///returns the type including template-parameters and pointer-depth
 QString fullName( ) const;
 
	/**returns the type include template-parameters, pointer-depth, and possible sub-types.
	   Example "A::B": A is the type, and B is the subtype */
 QString fullNameChain( ) const ;

	///Returns the type-structure(full name-chain without any instance-info)
 QString fullTypeStructure() const;
	
 int pointerDepth() const {
  return m_pointerDepth;
 }
 
 void setPointerDepth( int d ) {
  m_pointerDepth = d;
 }
 
 void decreasePointerDepth() {
  if( m_pointerDepth > 0 )
   m_pointerDepth--;
 }
 
	///returns a list include the full name of this type, and all subtypes
 QStringList fullNameList( ) const;
 
 QString name() const {
  return m_cleanName;
 };
 
 void setName( QString name ) {
  m_cleanName = name;
 }
 
 /** The template-params may be changed in-place
     this list is local, but the params pointed by them not(call makePrivate before changing) */
 TemplateParams& templateParams();
 
 const TemplateParams& templateParams() const;
 
 /**makes all references/pointers private, so everything about this structure may be changed without side-effects*/
 TypeDesc& makePrivate();
 
 operator bool () const {
  return !m_cleanName.isEmpty();
 }
 
 TypeDescPointer next();
 
 bool hasTemplateParams() const ;
 
 void setNext( TypeDescPointer type );
 
 void append( TypeDescPointer type );
 
 TypePointer resolved() const ;
 
 void setResolved( TypePointer resolved );
 
 void resetResolved();
 
 ///Resets the resolved-pointers of this type, and all template-types
 void resetResolvedComplete();
 
 void increaseFunctionDepth();
 
 void decreaseFunctionDepth();
 
 int functionDepth() const;

	void setFlag( Flags flag ) {
		m_flags = (Flags) (m_flags | flag);
	}

	bool hasFlag( Flags flag ) {
		return (bool)( m_flags & flag );
	}
	
	///instance-information consists of things like the pointer-depth and the decoration
 void takeInstanceInfo( const TypeDesc& rhs );
 
 void clearInstanceInfo();
};

class TypeDescShared : public TypeDesc, public KShared {
public:
 
 
 TypeDescShared( const TypeDescShared& rhs ) : TypeDesc(rhs), KShared() {
 }
 
 TypeDescShared( const TypeDesc& rhs ) : TypeDesc(rhs), KShared() {
 }
 
 TypeDescShared& operator = ( const TypeDesc& rhs ) {
  (*(TypeDesc*)this) = rhs;
  return *this;
 } 

 TypeDescShared( const QString& name = "" ) : TypeDesc( name ) {
 }
};

extern TypeDesc operator + ( const TypeDesc& lhs, const TypeDesc& rhs );

#endif
// kate: indent-mode csands; tab-width 4;
