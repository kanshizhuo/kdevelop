/* KDevelop CMake Support
 *
 * Copyright 2007 Aleix Pol <aleixpol@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "cmakecachemodel.h"
#include <QFile>
#include <KDebug>
#include <KLocalizedString>

#include "cmakecachereader.h"

//4 columns: name, type, value, comment
//name:type=value - comment
CMakeCacheModel::CMakeCacheModel(QObject *parent, const KUrl &path)
    : QStandardItemModel(parent), m_filePath(path)
{
    read();
}

void CMakeCacheModel::reset()
{
    emit beginResetModel();
    clear();
    m_internal.clear();
    read();
    emit endResetModel();
}

void CMakeCacheModel::read()
{
    // Set headers
    QStringList labels;
    labels.append(i18n("Name"));
    labels.append(i18n("Type"));
    labels.append(i18n("Value"));
    labels.append(i18n("Comment"));
    labels.append(i18n("Advanced"));
    setHorizontalHeaderLabels(labels);

    QFile file(m_filePath.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        kDebug(9032) << "error. Could not find the file";
        return;
    }

    int currentIdx=0;
    QStringList currentComment;
    QTextStream in(&file);
    QHash<QString, int> variablePos;
    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if(line.startsWith("//"))
            currentComment += line.right(line.count()-2);
        else if(!line.isEmpty() && !line.startsWith('#')) //it is a variable
        {
            CacheLine c;
            c.readLine(line);
            
            if(c.isCorrect())
            {
                QString name=c.name(), flag=c.flag();
                
                QString type=c.type();
                QString value=c.value();

                QList<QStandardItem*> lineItems;
                lineItems.append(new QStandardItem(name));
                lineItems.append(new QStandardItem(type));
                lineItems.append(new QStandardItem(value));
                lineItems.append(new QStandardItem(currentComment.join("\n")));

                if(flag=="INTERNAL")
                {
                    m_internal.insert(name);
                } else if(flag=="ADVANCED")
                {
                    if(variablePos.contains(name))
                    {
                        int pos=variablePos[name];
                        QStandardItem *p = item(pos, 4);
                        if(!p)
                        {
                            p=new QStandardItem(value);
                            setItem(pos, 4, p);
                        }
                        else
                        {
                            p->setText(value);
                        }
                    }
                    else
                    {
                        kDebug(9032) << "Flag for an unknown variable";
                    }
                }
                
                if(!flag.isEmpty())
                {
                    lineItems[0]->setText(lineItems[0]->text()+'-'+flag);
                }
                insertRow(currentIdx, lineItems);
                variablePos[name]=currentIdx;
                currentIdx++;
                currentComment.clear();
            }
        }
        else if(line.startsWith('#') && line.contains("INTERNAL"))
        {
            m_internalBegin=currentIdx;
//                 kDebug(9032) << "Comment: " << line << " -.- " << currentIdx;
        }
        else if(!line.startsWith('#') && !line.isEmpty())
        {
            kDebug(9032) << "unrecognized cache line: " << line;
        }
    }
}

bool CMakeCacheModel::writeBack(const KUrl & path) const
{
    kDebug(9042) << "writing CMakeCache.txt at " << path;
    QFile file(path.toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        kDebug(9032) << "Could not open " << path << " the file for writing";
        return false;
    }

    KUrl dir(path);
    dir.upUrl();
    QTextStream out(&file);
    out << "# This is the CMakeCache file." << endl;
    out << "# For build in directory: " << dir.pathOrUrl() << endl;
    out << "# It was generated by cmake and edited by KDevelop 4" << endl;
    out << "# You can edit this file to change values found and used by cmake." << endl;
    out << "# If you do not want to change any of the values, simply exit the editor." << endl;
    out << "# If you do want to change a value, simply edit, save, and exit the editor." << endl;
    out << "# The syntax for the file is as follows:" << endl;
    out << "# KEY:TYPE=VALUE" << endl;
    out << "# KEY is the name of a variable in the cache." << endl;
    out << "# TYPE is a hint to GUI's for the type of VALUE, DO NOT EDIT TYPE!." << endl;
    out << "# VALUE is the current value for the KEY." << endl << endl;

    out << "########################" << endl;
    out << "# EXTERNAL cache entries" << endl;
    out << "########################" << endl << endl;
    for(int i=0; i<rowCount(); i++)
    {
        if(i==m_internalBegin)
        {
            out << endl;
            out << "########################" << endl;
            out << "# INTERNAL cache entries" << endl;
            out << "########################" << endl << endl;
        }
        
        QStandardItem* name = item(i, 0);
        QStandardItem* type = item(i, 1);
        QStandardItem* valu = item(i, 2);
        QStandardItem* comm = item(i, 3);
        if(!name || !type || !comm || !valu)
            continue;
        if(!comm->text().isEmpty())
        {
            QStringList comments=comm->text().split('\n');
            foreach(const QString& commLine, comments)
            {
                out << "//";
                out << commLine;
                out << endl;
            }
        }
        QString var=name->text();
        if(!type->text().isEmpty())
            var += ':'+type->text();
        var+='='+valu->text();
        out << var << endl;
        if(i<m_internalBegin)
            out << endl;
    }
    out << endl;
    return true;
}

QString CMakeCacheModel::value(const QString & varName) const
{
    for(int i=0; i<rowCount(); i++)
    {
        QStandardItem* name = item(i, 0);
        if(name->text()==varName) {
            QStandardItem* valu = item(i, 2);
            return valu->text();
        }
    }
    return QString();
}

bool CMakeCacheModel::isAdvanced(int i) const
{
    QStandardItem *p=item(i, 4);
    bool isAdv= (p!=0) || i>m_internalBegin;
    if(!isAdv)
    {
        p=item(i, 1);
        isAdv = p->text()=="INTERNAL" || p->text()=="STATIC";
    }
    
    if(!isAdv)
    {
        m_internal.contains(item(i,0)->text());
    }
    return isAdv;
}

bool CMakeCacheModel::isInternal(int i) const
{
    bool isInt= i>m_internalBegin;
    return isInt;
}

QList< QModelIndex > CMakeCacheModel::persistentIndices() const
{
    QList< QModelIndex > ret;
    for(int i=0; i<rowCount(); i++)
    {
        QStandardItem* type = item(i, 1);
        if(type->text()=="BOOL")
        {
            QStandardItem* valu = item(i, 2);
            ret.append(valu->index());
        }
    }
    return ret;
}

KUrl CMakeCacheModel::filePath() const
{
    return m_filePath;
}


