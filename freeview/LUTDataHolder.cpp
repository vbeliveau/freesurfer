/**
 * @file  LUTDataHolder.h
 * @brief LUT data object.
 *
 */
/*
 * Original Author: Ruopeng Wang
 * CVS Revision Info:
 *    $Author: krish $
 *    $Date: 2011/03/12 00:28:48 $
 *    $Revision: 1.15 $
 *
 * Copyright (C) 2008-2009,
 * The General Hospital Corporation (Boston, MA).
 * All rights reserved.
 *
 * Distribution, usage and copying of this software is covered under the
 * terms found in the License Agreement file named 'COPYING' found in the
 * FreeSurfer source code root directory, and duplicated here:
 * https://surfer.nmr.mgh.harvard.edu/fswiki/FreeSurferOpenSourceLicense
 *
 * General inquiries: freesurfer@nmr.mgh.harvard.edu
 * Bug reports: analysis-bugs@nmr.mgh.harvard.edu
 *
 */

#include "LUTDataHolder.h"
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QProcessEnvironment>
#include <QDir>
#include "MyUtils.h"
#include <iostream>

LUTDataHolder::LUTDataHolder()
{
  ColorTableData ctd;
  QString fs_home = QProcessEnvironment::systemEnvironment().value( "FREESURFER_HOME" );
  QFileInfo fi( fs_home + "/FreeSurferColorLUT.txt" );
  if (fi.exists())
  {
      ctd.filename = fi.absoluteFilePath();
      ctd.table = CTABreadASCII( ctd.filename.toAscii().data() );
      ctd.name = "FreeSurferColorLUT";
      if ( ctd.table )
        m_tables.push_back( ctd );
  }

  fi.setFile( fs_home + "/tkmeditParcColorsCMA" );
  if (fi.exists())
  {
      ctd.filename = fi.absoluteFilePath();
      ctd.table = CTABreadASCII( ctd.filename.toAscii().data() );
      ctd.name = "tkmeditParcColorsCMA";
      if ( ctd.table )
        m_tables.push_back( ctd );
  }

  fi.setFile( fs_home + "/Simple_surface_labels2009.txt" );
  if (fi.exists())
  {
      ctd.filename = fi.absoluteFilePath();
      ctd.table = CTABreadASCII( ctd.filename.toAscii().data() );
      ctd.name = "Simple_surface_labels2009";
      if ( ctd.table )
        m_tables.push_back( ctd );
  }

  if ( m_tables.isEmpty() )
  {
    QFile file_in( ":/FreeSurferColorLUT.txt" );
    file_in.open(QIODevice::ReadOnly | QIODevice::Text);
    QString tempfn = QDir::tempPath() + "/FreeSurferColorLUT.txt";
#ifdef Q_CYGWIN_WIN
    tempfn = MyUtils::NormalizeCygwinPath(tempfn);
#endif
    QFile file_out( tempfn );
    file_out.open(QIODevice::WriteOnly | QIODevice::Text);
    file_out.write( file_in.readAll() );
    file_out.flush();
    file_out.close();

    ctd.filename = "FreeSurferColorLUT.txt";
    ctd.table = CTABreadASCII( tempfn.toAscii().constData() );
    ctd.name = "FreeSurferColorLUT";
    if ( ctd.table )
       m_tables.push_back( ctd );
    else
        std::cerr << "Error: Did not find any look up table files.\n";
  }
}

LUTDataHolder::~LUTDataHolder()
{
  for ( int i = 0; i < GetCount(); i++ )
  {
    CTABfree( &m_tables[i].table );
  }
}

QString LUTDataHolder::GetName( int i )
{
  if ( i < GetCount() )
    return m_tables[i].name;
  else
    return "";
}

COLOR_TABLE* LUTDataHolder::GetColorTable( int i )
{
  if ( i < GetCount() )
    return m_tables[i].table;
  else
    return NULL;
}

COLOR_TABLE* LUTDataHolder::GetColorTable( const QString& name )
{
  for ( int i = 0; i < GetCount(); i++ )
  {
    if ( m_tables[i].name == name ||
         QFileInfo(m_tables[i].filename).canonicalFilePath() == QFileInfo(name).canonicalFilePath() )
      return m_tables[i].table;
  }
  
  // if input is an index number instead of name, may still be valid
  bool bOK;
  int n = name.toInt( &bOK );
  if ( bOK && n < GetCount() )
    return m_tables[n].table;
  
  return NULL;
}

COLOR_TABLE* LUTDataHolder::LoadColorTable( const QString& filename )
{
  // first trying to see if we've already loaded the lut from the same file
  COLOR_TABLE* ct = GetColorTable( filename );
  QString filename_full = filename;
  if ( !ct )
  {
    QFileInfo fi( filename );
    if ( !fi.exists() )
      fi.setFile( QString("$FREESURFER_HOME/") + filename );
    filename_full = fi.canonicalFilePath();
    
    for ( int i = 0; i < GetCount(); i++ )
    {
      if ( m_tables[i].filename == filename_full )
      {
        ct = m_tables[i].table;
        break;
      }
    }
  }
  
  // if so, reload the lut file and update the lut data. do not create new entry in the lut list
  if ( ct )
  {
    int nId = -1;
    for ( int i = 0; i < GetCount(); i++ )
    {
      if ( m_tables[i].table == ct )
      {
        nId = i;
        break;
      }
    }
    
    ct = CTABreadASCII( m_tables[nId].filename.toAscii().data() );
    if ( ct )
    {
      CTABfree( &m_tables[nId].table );
      m_tables[nId].table = ct;
    }
    else
    {
        std::cerr << "Can not load color table from '" << qPrintable(filename) << "'.\n";
    }
  }
  // otherwise, load and create a new lut entry
  else
  {  
    ct = CTABreadASCII( filename_full.toAscii().data() );
    if ( ct )
    {
      ColorTableData ctd;
      ctd.table = ct;
      ctd.filename = filename_full;
      QFileInfo fi( filename_full );
      ctd.name = fi.completeBaseName();
      int n = 2;
      while ( GetColorTable( ctd.name ) )
      {
        ctd.name = fi.completeBaseName() + "_" + QString::number( n );
        n++;
      }
      m_tables.push_back( ctd );
    }
    else
    {
        std::cerr << "Can not load color table from '" << qPrintable(filename) << "'.\n";
    }
  }
  
  return ct;
}

int LUTDataHolder::GetCount()
{
  return m_tables.size();
}

int LUTDataHolder::GetIndex( COLOR_TABLE* ct )
{
  for ( int i = 0; i < GetCount(); i++ )
  {
    if ( ct == m_tables[i].table )
      return i;
  }

  return -1;
}
