/***************************************************************************
    kadasmaptooledititem.cpp
    ------------------------
    copyright            : (C) 2019 by Sandro Mani
    email                : smani at sourcepole dot ch
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QMenu>
#include <QPushButton>

#include <qgis/qgsmapcanvas.h>
#include <qgis/qgsmapmouseevent.h>
#include <qgis/qgsproject.h>
#include <qgis/qgssettings.h>

#include <kadas/gui/kadasbottombar.h>
#include <kadas/gui/kadasclipboard.h>
#include <kadas/gui/kadasfloatinginputwidget.h>
#include <kadas/gui/kadasitemlayer.h>
#include <kadas/gui/kadasmapcanvasitemmanager.h>
#include <kadas/gui/mapitems/kadasmapitem.h>
#include <kadas/gui/mapitemeditors/kadasmapitemeditor.h>
#include <kadas/gui/maptools/kadasmaptoolcreateitem.h>
#include <kadas/gui/maptools/kadasmaptooledititem.h>
#include <kadas/gui/maptools/kadasmaptooledititemgroup.h>


KadasMapToolEditItem::KadasMapToolEditItem( QgsMapCanvas *canvas, const QString &itemId, KadasItemLayer *layer )
  : QgsMapTool( canvas )
  , mLayer( layer )
{
  mItem = layer->takeItem( itemId );
  layer->triggerRepaint();
  KadasMapCanvasItemManager::addItem( mItem );
}

KadasMapToolEditItem::KadasMapToolEditItem( QgsMapCanvas *canvas, KadasMapItem *item, KadasItemLayer *layer )
  : QgsMapTool( canvas )
  , mLayer( layer )
  , mItem( item )
{
  KadasMapCanvasItemManager::addItem( mItem );
}

void KadasMapToolEditItem::activate()
{
  QgsMapTool::activate();
  setCursor( Qt::ArrowCursor );
  mStateHistory = new KadasStateHistory( this );
  mStateHistory->push( mItem->constState()->clone() );
  connect( mStateHistory, &KadasStateHistory::stateChanged, this, &KadasMapToolEditItem::stateChanged );

  mBottomBar = new KadasBottomBar( canvas() );
  mBottomBar->setLayout( new QHBoxLayout() );
  mBottomBar->layout()->setContentsMargins( 8, 4, 8, 4 );
  if ( mItem->getEditorFactory() )
  {
    mEditor = mItem->getEditorFactory()( mItem, KadasMapItemEditor::EditItemEditor );
    mEditor->syncItemToWidget();
    mBottomBar->layout()->addWidget( mEditor );
  }
  mItem->setSelected( true );

  QPushButton *undoButton = new QPushButton();
  undoButton->setIcon( QIcon( ":/kadas/icons/undo" ) );
  undoButton->setToolTip( tr( "Undo" ) );
  undoButton->setEnabled( false );
  connect( undoButton, &QPushButton::clicked, this, [this] { mStateHistory->undo(); } );
  connect( mStateHistory, &KadasStateHistory::canUndoChanged, undoButton, &QPushButton::setEnabled );
  mBottomBar->layout()->addWidget( undoButton );

  QPushButton *redoButton = new QPushButton();
  redoButton->setIcon( QIcon( ":/kadas/icons/redo" ) );
  redoButton->setToolTip( tr( "Redo" ) );
  redoButton->setEnabled( false );
  connect( redoButton, &QPushButton::clicked, this, [this] { mStateHistory->redo(); } );
  connect( mStateHistory, &KadasStateHistory::canRedoChanged, redoButton, &QPushButton::setEnabled );
  mBottomBar->layout()->addWidget( redoButton );

  QPushButton *closeButton = new QPushButton();
  closeButton->setIcon( QIcon( ":/kadas/icons/close" ) );
  closeButton->setToolTip( tr( "Close" ) );
  connect( closeButton, &QPushButton::clicked, this, [this] { canvas()->unsetMapTool( this ); } );
  mBottomBar->layout()->addWidget( closeButton );

  mBottomBar->show();
}

void KadasMapToolEditItem::deactivate()
{
  QgsMapTool::deactivate();
  if ( mItem )
  {
    mLayer->addItem( mItem );
    mLayer->triggerRepaint();
    KadasMapItem *item = mItem;
    QObject *scope = new QObject;
    connect( mCanvas, &QgsMapCanvas::mapCanvasRefreshed, scope, [item, scope] { KadasMapCanvasItemManager::removeItem( item ); scope->deleteLater(); } );
    mItem->setSelected( false );
  }
  delete mBottomBar;
  mBottomBar = nullptr;
  delete mStateHistory;
  mStateHistory = nullptr;
  delete mInputWidget;
  mInputWidget = nullptr;
}

void KadasMapToolEditItem::canvasPressEvent( QgsMapMouseEvent *e )
{
  if ( e->button() == Qt::LeftButton && e->modifiers() == Qt::ControlModifier )
  {
    QString itemId = mLayer->pickItem( e->mapPoint(), mCanvas->mapSettings() );
    if ( !itemId.isEmpty() )
    {
      KadasMapItem *otherItem = mLayer->takeItem( itemId );
      KadasMapCanvasItemManager::removeItem( mItem );
      KadasMapItem *item = mItem;
      mItem = nullptr;
      mLayer->triggerRepaint();
      mCanvas->setMapTool( new KadasMapToolEditItemGroup( mCanvas, QList<KadasMapItem *>() << item << otherItem, mLayer ) );
    }
  }
  if ( e->button() == Qt::RightButton )
  {
    if ( mEditContext.isValid() )
    {
      QMenu menu;
      mItem->populateContextMenu( &menu, mEditContext );
      menu.addAction( tr( "Cut %1" ).arg( mItem->itemName() ), this, &KadasMapToolEditItem::cutItem );
      menu.addAction( tr( "Copy %1" ).arg( mItem->itemName() ), this, &KadasMapToolEditItem::copyItem );
      menu.addAction( tr( "Delete %1" ).arg( mItem->itemName() ), this, &KadasMapToolEditItem::deleteItem );
      QAction *clickedAction = menu.exec( e->globalPos() );

      if ( clickedAction )
      {
        if ( clickedAction->data() == KadasMapItem::EditSwitchToDrawingTool )
        {
          KadasMapCanvasItemManager::removeItem( mItem );
          KadasMapItem *item = mItem;
          mItem = nullptr;
          canvas()->setMapTool( new KadasMapToolCreateItem( canvas(), item, mLayer ) );
        }
      }
    }
    else
    {
      canvas()->unsetMapTool( this );
    }
  }
}

void KadasMapToolEditItem::canvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( mIgnoreNextMoveEvent )
  {
    mIgnoreNextMoveEvent = false;
    return;
  }
  QgsCoordinateTransform crst( canvas()->mapSettings().destinationCrs(), mItem->crs(), QgsProject::instance() );
  QgsPointXY pos = crst.transform( e->mapPoint() );

  if ( e->buttons() == Qt::LeftButton )
  {
    if ( mEditContext.isValid() )
    {
      mItem->edit( mEditContext, pos - mMoveOffset, canvas()->mapSettings() );
    }
  }
  else
  {
    KadasMapItem::EditContext oldContext = mEditContext;
    mEditContext = mItem->getEditContext( pos, canvas()->mapSettings() );
    if ( !mEditContext.isValid() )
    {
      setCursor( Qt::ArrowCursor );
      clearNumericInput();
    }
    else
    {
      if ( mEditContext != oldContext )
      {
        setCursor( mEditContext.cursor );
        setupNumericInput();
      }
      mMoveOffset = pos - mEditContext.pos;
    }
  }
  if ( mInputWidget && mEditContext.isValid() )
  {
    mInputWidget->ensureFocus();

    KadasMapItem::AttribValues values = mItem->editAttribsFromPosition( mEditContext, pos - mMoveOffset );
    QgsPointXY coo;
    int xCooId = -1;
    int yCooId = -1;
    double distanceConv = QgsUnitTypes::fromUnitToUnitFactor( QgsUnitTypes::DistanceMeters, canvas()->mapUnits() );
    for ( auto it = values.begin(), itEnd = values.end(); it != itEnd; ++it )
    {
      // This assumes that there is never more that one x-y coordinate pair in the attributes
      if ( mEditContext.attributes[it.key()].type == KadasMapItem::NumericAttribute::XCooAttr )
      {
        coo.setX( it.value() );
        xCooId = it.key();
      }
      else if ( mEditContext.attributes[it.key()].type == KadasMapItem::NumericAttribute::YCooAttr )
      {
        coo.setY( it.value() );
        yCooId = it.key();
      }
      else if ( mEditContext.attributes[it.key()].type == KadasMapItem::NumericAttribute::DistanceAttr )
      {
        it.value() *= distanceConv;
      }
      mInputWidget->inputField( it.key() )->setValue( it.value() );
    }
    if ( xCooId >= 0 && yCooId >= 0 )
    {
      QgsCoordinateTransform crst( mItem->crs(), canvas()->mapSettings().destinationCrs(), QgsProject::instance()->transformContext() );
      coo = crst.transform( coo );
      mInputWidget->inputField( xCooId )->setValue( coo.x() );
      mInputWidget->inputField( yCooId )->setValue( coo.y() );
    }

    mInputWidget->move( e->x(), e->y() + 20 );
    mInputWidget->show();
    if ( mInputWidget->focusedInputField() )
    {
      mInputWidget->focusedInputField()->setFocus();
      mInputWidget->focusedInputField()->selectAll();
    }
  }
}

void KadasMapToolEditItem::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( e->button() == Qt::LeftButton && mEditContext.isValid() )
  {
    mStateHistory->push( mItem->constState()->clone() );
  }
}

void KadasMapToolEditItem::keyPressEvent( QKeyEvent *e )
{
  if ( e->key() == Qt::Key_Escape )
  {
    canvas()->unsetMapTool( this );
  }
  else if ( e->key() == Qt::Key_Z && e->modifiers() == Qt::ControlModifier )
  {
    mStateHistory->undo();
  }
  else if ( e->key() == Qt::Key_Y && e->modifiers() == Qt::ControlModifier )
  {
    mStateHistory->redo();
  }
  else if ( e->key() == Qt::Key_Delete )
  {
    deleteItem();
  }
}

void KadasMapToolEditItem::setupNumericInput()
{
  clearNumericInput();
  if ( QgsSettings().value( "/kadas/showNumericInput", false ).toBool() && mEditContext.isValid() && !mEditContext.attributes.isEmpty() )
  {
    mInputWidget = new KadasFloatingInputWidget( canvas() );

    const KadasMapItem::AttribDefs &attributes = mEditContext.attributes;
    for ( auto it = attributes.begin(), itEnd = attributes.end(); it != itEnd; ++it )
    {
      const KadasMapItem::NumericAttribute &attribute = it.value();
      KadasFloatingInputWidgetField *attrEdit = new KadasFloatingInputWidgetField( it.key(), attribute.decimals, attribute.min, attribute.max );
      connect( attrEdit, &KadasFloatingInputWidgetField::inputChanged, this, &KadasMapToolEditItem::inputChanged );
      mInputWidget->addInputField( attribute.name + ":", attrEdit );
    }
    if ( !attributes.isEmpty() )
    {
      mInputWidget->setFocusedInputField( mInputWidget->inputField( attributes.begin().key() ) );
    }
  }
}

void KadasMapToolEditItem::clearNumericInput()
{
  delete mInputWidget;
  mInputWidget = nullptr;
}

void KadasMapToolEditItem::stateChanged( KadasStateHistory::State *state )
{
  mItem->setState( static_cast<const KadasMapItem::State *>( state ) );
}

KadasMapItem::AttribValues KadasMapToolEditItem::collectAttributeValues() const
{
  QgsPointXY coo;
  int xCooId = -1;
  int yCooId = -1;
  KadasMapItem::AttribValues attributes;
  double distanceConv = QgsUnitTypes::fromUnitToUnitFactor( canvas()->mapUnits(), QgsUnitTypes::DistanceMeters );

  for ( const KadasFloatingInputWidgetField *field : mInputWidget->inputFields() )
  {
    double value = field->text().toDouble();
    // This assumes that there is never more that one x-y coordinate pair in the attributes
    if ( mEditContext.attributes[field->id()].type == KadasMapItem::NumericAttribute::XCooAttr )
    {
      coo.setX( value );
      xCooId = field->id();
    }
    else if ( mEditContext.attributes[field->id()].type == KadasMapItem::NumericAttribute::YCooAttr )
    {
      coo.setY( value );
      yCooId = field->id();
    }
    else if ( mEditContext.attributes[field->id()].type == KadasMapItem::NumericAttribute::DistanceAttr )
    {
      value *= distanceConv;
    }
    attributes.insert( field->id(), value );
  }
  if ( xCooId >= 0 && yCooId >= 0 )
  {
    QgsCoordinateTransform crst( canvas()->mapSettings().destinationCrs(), mItem->crs(), QgsProject::instance()->transformContext() );
    coo = crst.transform( coo );
    attributes.insert( xCooId, coo.x() );
    attributes.insert( yCooId, coo.y() );
  }
  return attributes;
}

void KadasMapToolEditItem::inputChanged()
{
  if ( mEditContext.isValid() )
  {

    KadasMapItem::AttribValues values = collectAttributeValues();

    // Ignore the move event emitted by re-positioning the mouse cursor:
    // The widget mouse coordinates (stored in a integer QPoint) loses precision,
    // and mapping it back to map coordinates in the mouseMove event handler
    // results in a position different from geoPos, and hence the user-input
    // may get altered
    mIgnoreNextMoveEvent = true;

    QgsPointXY newPos = mItem->positionFromEditAttribs( mEditContext, values, mCanvas->mapSettings() );
    QgsCoordinateTransform crst( mItem->crs(), mCanvas->mapSettings().destinationCrs(), QgsProject::instance() );
    mInputWidget->adjustCursorAndExtent( crst.transform( newPos ) );

    mItem->edit( mEditContext, values, canvas()->mapSettings() );
    mStateHistory->push( mItem->constState()->clone() );
  }
}

void KadasMapToolEditItem::copyItem()
{
  KadasClipboard::instance()->setStoredMapItems( QList<KadasMapItem *>() << mItem );
}

void KadasMapToolEditItem::cutItem()
{
  KadasClipboard::instance()->setStoredMapItems( QList<KadasMapItem *>() << mItem );
  deleteItem();
}

void KadasMapToolEditItem::deleteItem()
{
  delete mEditor;
  mEditor = nullptr;

  delete mItem;
  mItem = nullptr;
  canvas()->unsetMapTool( this );
}
