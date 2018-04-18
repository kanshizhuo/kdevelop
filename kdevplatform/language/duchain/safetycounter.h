
/***************************************************************************
   Copyright 2006 David Nolden <david.nolden.kdevelop@art-master.de>
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef __SAFETYCOUNTER_H__
#define __SAFETYCOUNTER_H__

#include <QDebug>

struct SafetyCounter {
  int safetyCounter = 0;
  const int maxSafetyCounter;

  explicit SafetyCounter( int max = 40000 ) : maxSafetyCounter(max) {
  }

  void init() {
    safetyCounter = 0;
  }

  SafetyCounter& operator ++() {
    safetyCounter++;
    return *this;
  }

  ///Returns whether the counter is ok, but without increasing it
  bool ok() const {
    return safetyCounter < maxSafetyCounter;
  }

  operator bool() {
    safetyCounter++;
    bool ret = safetyCounter < maxSafetyCounter;
    if( !ret ) {
      if( safetyCounter == maxSafetyCounter ) {
#ifdef DEPTHBACKTRACE
        qDebug() << "WARNING: Safety-counter reached count > " << maxSafetyCounter << ", operation stopped";
#endif
      }
    }

    return ret;
  }

};

#endif
