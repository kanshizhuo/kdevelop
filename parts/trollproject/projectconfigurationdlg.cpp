/***************************************************************************
 *   Copyright (C) 2001 by Jakob Simon-Gaarde                              *
 *   jsgaarde@tdcspace.dk                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "projectconfigurationdlg.h"
#include <qradiobutton.h>
#include <qcheckbox.h>
#include <qmessagebox.h>
#include <qfiledialog.h>
#include <klineedit.h>
#include <qpushbutton.h>

ProjectConfigurationDlg::ProjectConfigurationDlg(ProjectConfiguration *conf,QWidget* parent, const char* name, bool modal, WFlags fl)
: ProjectConfigurationDlgBase(parent,name,modal,fl)
//=================================================
{
  m_projectConfiguration = conf;
  UpdateControls();
}


ProjectConfigurationDlg::~ProjectConfigurationDlg()
//==============================================
{
}


void ProjectConfigurationDlg::browseTargetPath()
//==============================================
{
  m_targetPath->setText(QFileDialog::getExistingDirectory());

}

void ProjectConfigurationDlg::updateProjectConfiguration()
//=======================================================
{
  // Template
  if (radioApplication->isChecked())
    m_projectConfiguration->m_template = QTMP_APPLICATION;
  else if (radioLibrary->isChecked())
    m_projectConfiguration->m_template = QTMP_LIBRARY;
  else if (radioSubdirs->isChecked())
    m_projectConfiguration->m_template = QTMP_SUBDIRS;

  // Buildmode
  if (radioDebugMode->isChecked())
    m_projectConfiguration->m_buildMode = QBM_DEBUG;
  if (radioReleaseMode->isChecked())
    m_projectConfiguration->m_buildMode = QBM_RELEASE;

  // requirements
  m_projectConfiguration->m_requirements = 0;
  if (checkQt->isChecked())
    m_projectConfiguration->m_requirements += QD_QT;
  if (checkOpenGL->isChecked())
    m_projectConfiguration->m_requirements += QD_OPENGL;
  if (checkThread->isChecked())
    m_projectConfiguration->m_requirements += QD_THREAD;
  if (checkX11->isChecked())
    m_projectConfiguration->m_requirements += QD_X11;

  // Warnings
  m_projectConfiguration->m_warnings = QWARN_OFF;
  if (checkWarning->isChecked())
    m_projectConfiguration->m_warnings = QWARN_ON;

  m_projectConfiguration->m_target = "";
  if ((m_targetPath->text().simplifyWhiteSpace()!="" ||
      m_targetOutputFile->text().simplifyWhiteSpace()!="") &&
      !radioSubdirs->isChecked())
  {
    QString outputFile = m_targetOutputFile->text();
    if (outputFile.simplifyWhiteSpace() == "")
      outputFile = m_projectConfiguration->m_subdirName;
    m_projectConfiguration->m_target = m_targetPath->text() + "/" + outputFile;
  }

  QDialog::accept();
}


void ProjectConfigurationDlg::UpdateControls()
//============================================
{
  QRadioButton *activateRadiobutton=NULL;
  // Project template
  switch (m_projectConfiguration->m_template)
  {
    case QTMP_APPLICATION:
      activateRadiobutton = radioApplication;
      break;
    case QTMP_LIBRARY:
      activateRadiobutton = radioLibrary;
      break;
    case QTMP_SUBDIRS:
      activateRadiobutton = radioSubdirs;
      break;
  }
  // Buildmode
  if (activateRadiobutton)
    activateRadiobutton->setChecked(true);
  switch (m_projectConfiguration->m_buildMode)
  {
    case QBM_DEBUG:
      activateRadiobutton = radioDebugMode;
      break;
    case QBM_RELEASE:
      activateRadiobutton = radioReleaseMode;
      break;
  }
  if (activateRadiobutton)
    activateRadiobutton->setChecked(true);

  // Requirements
  if (m_projectConfiguration->m_requirements & QD_QT)
    checkQt->setChecked(true);
  if (m_projectConfiguration->m_requirements & QD_OPENGL)
    checkOpenGL->setChecked(true);
  if (m_projectConfiguration->m_requirements & QD_THREAD)
    checkThread->setChecked(true);
  if (m_projectConfiguration->m_requirements & QD_X11)
    checkX11->setChecked(true);

  // Warnings
  if (m_projectConfiguration->m_warnings == QWARN_ON)
  {
    checkWarning->setChecked(true);
  }

  QString targetString = m_projectConfiguration->m_target;
  int slashPos = targetString.findRev('/');

  if (slashPos>=0)
  {
    m_targetPath->setText(targetString.left(slashPos));
    m_targetOutputFile->setText(targetString.right(targetString.length()-slashPos-1));
  }
  else
    m_targetOutputFile->setText(targetString);
  clickSubdirsTemplate();
}

void ProjectConfigurationDlg::clickSubdirsTemplate()
{
  if (radioSubdirs->isChecked())
  {
    m_targetPath->setEnabled(false);
    m_targetOutputFile->setEnabled(false);
    Browse->setEnabled(false);
  }
  else
  {
    m_targetPath->setEnabled(true);
    m_targetOutputFile->setEnabled(true);
    Browse->setEnabled(true);
  }
}
