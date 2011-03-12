#include "UIUpdateHelper.h"
#include <QLineEdit>
#include <QDoubleSpinBox>

UIUpdateHelper::UIUpdateHelper()
{
}


void UIUpdateHelper::ShowWidgets( const QList<QWidget*>& list, bool bShow )
{
  for ( int i = 0; i < list.size(); i++ )
  {
    list[i]->setVisible( bShow );
  }
}

void UIUpdateHelper::EnableWidgets( const QList<QWidget*>& list, bool bEnable )
{
  for ( int i = 0; i < list.size(); i++ )
  {
    list[i]->setEnabled( bEnable );
  }
}

void UIUpdateHelper::ChangeLineEditText( QLineEdit* w, const QString& strg )
{
  if ( w->text() == strg )
    return;

  int n = w->cursorPosition();
  w->blockSignals( true );
  w->setText( strg );
  w->setCursorPosition( n );
  w->blockSignals( false );
}

void UIUpdateHelper::ChangeLineEditNumber( QLineEdit* w, double val )
{
    bool bOK;
    double temp_val = w->text().toDouble(&bOK);
    if ( bOK && temp_val == val )
        return;

    QString strg;
    if (val < 1)
        strg = QString("%1").arg(val);
    else
    {
        strg = QString::number(val, 'f', (val>100?2:-1));
        while (strg[strg.size()-1] == '0')
        {
            strg = strg.left(strg.size()-1);
        }
        if (strg[strg.size()-1] == '.')
            strg = strg.left(strg.size()-1);
    }
  ChangeLineEditText(w, strg);
}

void UIUpdateHelper::ChangeSpinBoxValue( QSpinBox* w, int nVal )
{
  if ( w->value() == nVal )
    return;

  w->blockSignals( true );
  w->setValue( nVal );
  w->blockSignals( false );
}

void UIUpdateHelper::ChangeDoubleSpinBoxValue( QDoubleSpinBox* w, double dVal )
{
  if ( w->value() == dVal )
    return;

  w->blockSignals(true);
  w->setValue( dVal );
  w->blockSignals(false);
}

void UIUpdateHelper::BlockAllSignals(QWidget* w, bool bBlock)
{
    QWidgetList widgets = w->findChildren<QWidget*>();
    for (int i = 0; i < widgets.size(); i++)
        widgets[i]->blockSignals(bBlock);
}
