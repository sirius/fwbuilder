/*

                          Firewall Builder

                 Copyright (C) 2003 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

  $Id$

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/


#include "../../config.h"
#include "global.h"
#include "utils.h"

#include "FWWindow.h"
#include "FWBTree.h"
#include "ObjectTreeView.h"
#include "ObjectTreeViewItem.h"
#include "ObjectManipulator.h"
#include "FWObjectDrag.h"
#include "FWBSettings.h"
#include "IconSetter.h"
#include "FWObjectPropertiesFactory.h"

#include "fwbuilder/FWObject.h"
#include "fwbuilder/Firewall.h"
#include "fwbuilder/Resources.h"
#include "fwbuilder/Group.h"
#include "fwbuilder/Interface.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QBitmap>
#include <qpainter.h>
#include <qpixmapcache.h>
#include <QMimeData>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QFocusEvent>
#include <QPixmap>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QtDebug>

#include <iostream>
#include <algorithm>
#include "ProjectPanel.h"

using namespace std;
using namespace libfwbuilder;

/****************************************************************************
 *
 *    class ObjectTreeView
 *
 ****************************************************************************/

ObjectTreeView::ObjectTreeView(ProjectPanel* project,
                               QWidget* parent,
                               const char * name,
                               Qt::WFlags f) :
    QTreeWidget(parent), 
    m_project(project)
{
    setObjectName(name);
    this->setParent(parent, f);
    setFont(st->getTreeFont());
//    setAcceptDrops( TRUE );
    item_before_drag_started=NULL;
    lastSelected = NULL;
    second_click = false;
    selectionFrozen = false;
    expandOrCollapse = false;
    Lockable = false;
    Unlockable = false;
    visible = false;
    /*
     * note about process_mouse_release_event
     *
     * we use mouseReleaseEvent event to switch object opened in the
     * editor panel (i.e. we open new object when mouse button is
     * released rather than when it is pressed). This allows us to
     * start drag without switching object in the editor. The problem
     * is that mouseReleaseEvent is received in this widget after the
     * d&d ends with a drop somewhere else, which triggers call to
     * mouseReleaseEvent which switches object in the
     * editor. This is undesired when the editor shows a group and we
     * try to drag and drop an object into that group. Flag
     * process_mouse_release_event is used to suppress object
     * switching when mouseReleaseEvent is called after
     * successfull drop.
     */
    process_mouse_release_event = true;

    connect(this, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
            this, SLOT(currentItemChanged(QTreeWidgetItem*)));

    connect(this, SIGNAL(itemSelectionChanged()),
            this, SLOT(itemSelectionChanged()));

    connect(this, SIGNAL(itemCollapsed(QTreeWidgetItem*)),
            this, SLOT(itemCollapsed(QTreeWidgetItem*)));

    connect(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
            this, SLOT(itemExpanded(QTreeWidgetItem*)));

    QStringList qsl;
    qsl.push_back(tr("Object"));
    qsl.push_back(tr("Attributes"));
    setHeaderLabels(qsl);

    //header()->hide();

    header()->setDefaultAlignment(Qt::AlignHCenter);
    header()->setResizeMode(QHeaderView::Interactive);

    showOrHideAttributesColumn();

    setMinimumSize( QSize( 100, 0 ) );

    setAutoScroll( TRUE );
    setAllColumnsShowFocus( TRUE );
    setSelectionMode( ExtendedSelection );
    setAcceptDrops( true );
    setDragDropMode( QAbstractItemView::DragDrop );
    setRootIsDecorated( TRUE );

    connect(this, SIGNAL(itemChanged(QTreeWidgetItem*,int)), this,
            SLOT(updateFilter()));

// disable sorting, otherwise gui crashes when built with
// QT 4.3.4 (discovered on Ubuntu Hardy). Crash happened when
// second object was added to any branch of the tree.
//
//    setSortingEnabled(true);
}

bool ObjectTreeView::event( QEvent *event )
{
    if (event->type() == QEvent::ToolTip)
    {
        QHelpEvent *he = (QHelpEvent*) event;
        QPoint pos = he->pos();

        if (st->getObjTooltips())
        {
            int cx = pos.x(), cy = pos.y();

            FWObject  *obj=NULL;
            QRect      cr;

            QTreeWidgetItem *itm = itemAt(QPoint(cx, cy - header()->height()));
            if (itm==NULL) return false;
            ObjectTreeViewItem *oivi  = dynamic_cast<ObjectTreeViewItem*>(itm);
            assert(oivi!=NULL);
            obj = oivi->getFWObject();

            if (obj==NULL) return false;

            if (obj->getId() == FWObjectDatabase::ANY_ADDRESS_ID ||
                obj->getId() == FWObjectDatabase::ANY_SERVICE_ID ||
                obj->getId() == FWObjectDatabase::ANY_INTERVAL_ID)
                return false;

            cr = visualItemRect(itm);

            QRect global = QRect(
                viewport()->mapToGlobal(cr.topLeft()),
                viewport()->mapToGlobal(cr.bottomRight()));

            //finally stretch rect up to component's width and even more
            //(it fixes bug with horizontal scroll)
            global.setWidth(width() + horizontalOffset());

            QToolTip::showText(mapToGlobal( he->pos() ),
                FWObjectPropertiesFactory::getObjectPropertiesDetailed(obj,
                                                                       true,
                                                                       true),
                this, global);
        }

        return true;
    }

    return QTreeWidget::event(event);
}

void ObjectTreeView::currentItemChanged(QTreeWidgetItem*)
{
    expandOrCollapse = false;
}

void ObjectTreeView::itemCollapsed(QTreeWidgetItem* itm)
{
    expandOrCollapse = true;

    ObjectTreeViewItem *otvi = dynamic_cast<ObjectTreeViewItem*>(itm);
    assert(otvi!=NULL);
    FWObject *o = otvi->getFWObject();
    if (o)
    {
        int id = o->getId();
        expanded_objects.erase(id);
    }
}

void ObjectTreeView::itemExpanded(QTreeWidgetItem* itm)
{
    expandOrCollapse = true;

    ObjectTreeViewItem *otvi=dynamic_cast<ObjectTreeViewItem*>(itm);
    assert(otvi!=NULL);
    FWObject *o = otvi->getFWObject();
    if (o)
    {
        int id = o->getId();
        expanded_objects.insert(id);
    }
}

/*
 * This method makes list selectedObjects flat. If user selects
 * several objects in the tree, and some of them have children, QT
 * puts all the children in the selected objects list even if
 * corresponding subtrees are collapsed. This method eliminates these
 * selected children objects.
 *
 */
std::vector<libfwbuilder::FWObject*> ObjectTreeView::getSimplifiedSelection()
{
    vector<FWObject*> so  = selectedObjects;
    vector<FWObject*> so2 = selectedObjects;
    for (vector<FWObject*>::iterator i=so2.begin();  i!=so2.end(); ++i)
    {
        for (vector<FWObject*>::iterator j=i;  j!=so2.end(); ++j)
        {
            vector<FWObject*>::iterator k=std::find(so.begin(),so.end(),*j);
            if ( (*j)->isChildOf( *i ) && k!=so.end())
                so.erase( k );
        }
    }
    return so;
}

FWObject* ObjectTreeView::getCurrentObject()
{
    QTreeWidgetItem *ovi = currentItem();
    ObjectTreeViewItem *otvi=dynamic_cast<ObjectTreeViewItem*>(ovi);
    if (otvi==NULL) return NULL;
    return otvi->getFWObject();
}

void ObjectTreeView::focusInEvent(QFocusEvent* ev)
{
    QTreeWidget::focusInEvent(ev);
    QTreeWidgetItem *ci = currentItem();
    if (ci) repaint();
}

void ObjectTreeView::focusOutEvent(QFocusEvent* ev)
{
    QTreeWidget::focusOutEvent(ev);
    QTreeWidgetItem *ci = currentItem();
    if (ci) repaint();
}

void ObjectTreeView::updateTreeIcons()
{
    if (fwbdebug) qDebug("ObjectTreeView::updateTreeIcons ");

    QTreeWidgetItemIterator it(this);
    while ( *it )
    {
        QTreeWidgetItem *itm = *it;
        ObjectTreeViewItem *otvi = dynamic_cast<ObjectTreeViewItem*>(itm);
        FWObject *obj = otvi->getFWObject();

        QPixmap pm_obj;
        IconSetter::setObjectIcon(obj, &pm_obj, 0);
        itm->setIcon(0, pm_obj );

        ++it;
    }
    update();
}

void ObjectTreeView::startDrag(Qt::DropActions supportedActions)
{
    QTreeWidgetItem *ovi = currentItem();
    if (ovi==NULL) return;

    ObjectTreeViewItem *otvi=dynamic_cast<ObjectTreeViewItem*>(ovi);

    FWObject *current_obj = getCurrentObject();

    if (fwbdebug) qDebug("ObjectTreeView::startDrag: this: %p current_obj: %s",
                         this, current_obj->getName().c_str());

/* can't drag system folders

    in fact, I have to allow to drag system folders because otherwise
    QListView triggers highlighting of objects in the tree when user
    drags mouse cursor across them. This is weird behavior and there
    does not seem to be any way to turn it off. It happens close to
    the end of void QListView::mouseMoveEvent( QMouseEvent * e)
    (See code after they decided that they do not need to call startDrag())

    if (FWBTree().isSystem(obj)) return NULL;
*/
    QString icn = (":/Icons/"+current_obj->getTypeName()+"/icon-ref").c_str();

    vector<FWObject*> so = getSimplifiedSelection();

    list<FWObject*> dragobj;
    for (vector<FWObject*>::iterator v=so.begin(); v!=so.end(); v++)
    {   
        //m_project->check4Depends(*v, dragobj);

        if (fwbdebug)
            qDebug("ObjectTreeView::startDrag: adding object to drag list: %s",
                   (*v)->getName().c_str());

        // while obj is still part of the tree, do some clean up
        // to avoid problems in the future.  Create
        // InterfaceOptions objects for interfaces because we'll
        // need them for various validations during paste/drop
        // operation.
        Interface *intf = Interface::cast(*v);
        if (intf) intf->getOptionsObject();

        dragobj.push_back( *v );
    }
    FWObjectDrag *drag = new FWObjectDrag(dragobj, this);

    QPixmap pm;
    if ( ! QPixmapCache::find( icn, pm) )
    {
        pm.load( icn );
        QPixmapCache::insert( icn, pm);
    }

    if (dragobj.size()>1)
    {
        QPixmap npm(32,32);
        QPainter p( &npm );
        p.fillRect( 0,0,32,32, QBrush( QColor("white"),Qt::SolidPattern ) );
        p.setBackgroundMode( Qt::TransparentMode );
        p.drawPixmap( 0, 32-pm.rect().height(), pm);
        p.setPen( QColor("red") );
        p.setBrush( QBrush( QColor("red"),Qt::SolidPattern ) );
        p.drawPie( 16, 0, 16,16, 0, 5760 );
        QString txt;
        txt.setNum(dragobj.size());
        QRect br=p.boundingRect(0, 0, 1000, 1000,
                                Qt::AlignLeft|Qt::AlignVCenter,
                                txt );
        p.setPen( QColor("white") );
        p.drawText( 24-br.width()/2 , 4+br.height()/2, txt );
        p.end();
        npm.setMask( npm.createHeuristicMask() );
        drag->setPixmap( npm );
    } else
        drag->setPixmap( pm );

/*
 * This fragment returns selection in the tree back to the object that
 * was selected before drag operation has started. This help in the
 * following case:
 *
 *  - open a group for editing (group is selected in the tree)
 *  - left-click on another object in the tree, start dragging it
 *
 * at this point selection in the tree returns to the group, so when
 * user finishes d&d operation, the selection in the tree is consisten
 * with object currently opened in the editor panel.
 *
 * There is a problem with this however. If user wants to put an
 * object from a different library into the group, they have to switch
 * to that library before doing d&d. When they switch, ObjectTree
 * shown in the left panel becomes different from the tree in which
 * the group is located. When d&d finishes, the ObjectTree object
 * receives mouseReleaseEvent event. Since it is not the right
 * tree object, it can not properly restore selection and choses an
 * object that was previously opened in that tree, which in turn
 * changes the object opened in the editor panel. To make things
 * worse, this event is only delivered to the tree object on Mac OS X.
 *
 *
 */
    if (fwbdebug) qDebug("ObjectTreeView::dragObject()  this=%p visible=%d",
                         this,visible);

    FWObject *edit_obj = mw->getOpenedEditor();

    if (mw->isEditorVisible() &&
        dragobj.size()==1 &&
        edit_obj!=NULL &&
        current_obj->getLibrary()==edit_obj->getLibrary() )
    {
        if (fwbdebug) qDebug("ObjectTreeView::dragObject() reset selection");
        otvi->setSelected(false);
        resetSelection();
    }

    if (fwbdebug) qDebug("ObjectTreeView::dragObject()  returns !NULL");

    drag->start(supportedActions);
}

void ObjectTreeView::dragEnterEvent( QDragEnterEvent *ev)
{
    ev->setAccepted(ev->mimeData()->hasFormat(FWObjectDrag::FWB_MIME_TYPE) );
    ev->setDropAction(Qt::MoveAction);
}

bool ObjectTreeView::isCurrReadOnly(QDragMoveEvent *ev)
{
// the tree can accept drop only if it goes into a group and if that group
//  validates the object and tree is not read-only
    QTreeWidgetItem *ovi = itemAt(ev->pos());

    ObjectTreeViewItem *otvi=dynamic_cast<ObjectTreeViewItem*>(ovi);
    FWObject *trobj;
    if (otvi && (trobj = otvi->getFWObject()))
        return trobj->isReadOnly();
    return false;
}

void ObjectTreeView::dragMoveEvent( QDragMoveEvent *ev)
{
    QWidget *fromWidget = ev->source();

    // The source of DnD object must be the same instance of fwbuilder
    if (!fromWidget)
    {
        ev->setAccepted(false);
        return;
    }
   
    if (isCurrReadOnly(ev) ||
        !ev->mimeData()->hasFormat(FWObjectDrag::FWB_MIME_TYPE))
    {
        ev->setAccepted(false);
        return;
    }
    list<FWObject*> dragol;
    if (!FWObjectDrag::decode(ev, dragol))
        ev->setAccepted(false);
    for (list<FWObject*>::iterator i=dragol.begin();i!=dragol.end(); ++i)
    {
        FWObject *dragobj = *i;
        assert(dragobj!=NULL);

        if (FWBTree().isSystem(dragobj))
        {
// can not drop system folder anywhere 
            ev->setAccepted(false);
            return;
        }
    }

    ev->setAccepted(true);
}

/*
 * See ticket #483: d&d of objects inside the tree should not be allowed
 */
void ObjectTreeView::dropEvent(QDropEvent *ev)
{
    ev->setAccepted(false);
    return;
}

FWObject *ObjectTreeView::getDropTarget(QDropEvent *ev, FWObject*)
{
    QTreeWidgetItem *ovi = itemAt(ev->pos());

    ObjectTreeViewItem *otvi = dynamic_cast<ObjectTreeViewItem*>(ovi);
    if (otvi) return otvi->getFWObject();
    else return NULL;
}

void ObjectTreeView::dragLeaveEvent( QDragLeaveEvent *ev)
{
    QTreeWidget::dragLeaveEvent(ev);

    clearSelection();
}

void ObjectTreeView::mouseMoveEvent( QMouseEvent * e )
{
    QTreeWidget::mouseMoveEvent(e);
    if (e==NULL)  return;
}

void ObjectTreeView::mousePressEvent( QMouseEvent *e )
{
    if (fwbdebug) qDebug("ObjectTreeView::mousePressEvent");

    second_click = false;
    process_mouse_release_event = true;

    if (fwbdebug)
    {
        qDebug() << "ObjectTreeView::mousePressEvent :: currentItem="
                 << ((currentItem())?currentItem()->text(0):"nil");
        qDebug() << "ObjectTreeView::mousePressEvent :: lastSelected="
                 << ((lastSelected)?lastSelected->text(0):"nil");
    }

    lastSelected = currentItem();

    QTreeWidget::mousePressEvent(e);

    if (e->button() == Qt::LeftButton)
    {
        startingDrag = true;
    }

    if (e->button() == Qt::RightButton)
        emit contextMenuRequested_sign(e->pos());
}

/*
 * Two modes of operation of this widget:
 *
 * 1.  this widget can intercept single mouse click and return
 * selection back to the object that was current before it. If user
 * double ckicks mouse button, then this reset is not done and new
 * object is selected. This is done using timer.
 *
 * 2. this widget can act as usual QListView does, that is, select an object
 * on a single click.
 *
 * uncomment the line that starts timer for mode #1.
 *
 *
 * we use mouseReleaseEvent event to switch object opened in the
 * editor panel (i.e. we open new object when mouse button is released
 * rather than when it is pressed). This allows us to start drag
 * without switching object in the editor. The problem is that
 * mouseReleaseEvent is received in this widget after the d&d ends
 * with a drop somewhere else, which triggers call to
 * mouseReleaseEvent which switches object in the editor. This
 * is undesired when the editor shows a group and we try to drag and
 * drop an object into that group. Flag process_mouse_release_event is
 * used to suppress object switching when mouseReleaseEvent is
 * called after successfull drop.
 *
 */
void ObjectTreeView::mouseReleaseEvent( QMouseEvent *e )
{
    if (fwbdebug)
        qDebug("ObjectTreeView::mouseReleaseEvent 1 this=%p  process_mouse_release_event=%d",
               this,process_mouse_release_event);

    QTreeWidget::mouseReleaseEvent(e);

    if (!process_mouse_release_event)
    {
        // just do not switch object in the editor, otherwise
        // process this event as usual
        process_mouse_release_event = true;
        return;
    }

    if (fwbdebug)
        qDebug("ObjectTreeView::mouseReleaseEvent 2 selectedObjects.size()=%d getCurrentObject()=%p current object %s",
               int(selectedObjects.size()),
               getCurrentObject(),
               (getCurrentObject()!=NULL)?getCurrentObject()->getName().c_str():"nil");

    if (expandOrCollapse) return;  // user expanded or collapsed subtree,
                                   // no need to change object in the editor

// Experiment: single click on the object in the tree should not open
// it in the editor
#if 0
    if (selectedObjects.size()==1)
        emit switchObjectInEditor_sign( getCurrentObject() );
    else
    {
        // user selected multiple objects
        // do not let them if editor has unsaved changes
        //
        if (mw->isEditorVisible() && mw->isEditorModified())
            emit switchObjectInEditor_sign( getCurrentObject() );
        else
            mw->blankEditor();
    }
#endif
}

/*
 * normally QAbstractItemView::edit starts in-place editing. We use
 * double click to open object in a separate editor panel 
 */
bool ObjectTreeView::edit(const QModelIndex &index,
                          EditTrigger trigger, QEvent *event)
{
    if (fwbdebug) qDebug("ObjectTreeView::edit");
    if (trigger==QAbstractItemView::DoubleClicked) editCurrentObject();
    return QTreeWidget::edit(index, trigger, event);
}

/*
 * sends signal that should be connected to a slot in
 * ObjectManipulator which opens editor panel if it is closed and then
 * opens current object in it
 */
void ObjectTreeView::editCurrentObject()
{
    if (fwbdebug) qDebug("ObjectTreeView::editCurrentObject");
    emit editCurrentObject_sign();
    if (fwbdebug) qDebug("ObjectTreeView::editCurrentObject done");
}

void ObjectTreeView::keyPressEvent( QKeyEvent* ev )
{
    FWObject *obj = getCurrentObject();
    if (obj)
    {
        if (ev->key()==Qt::Key_Enter || ev->key()==Qt::Key_Return)
        {
            if (fwbdebug)
                qDebug() << "ObjectTreeView::keyPressEvent Qt::Key_Enter";
            editCurrentObject();
            ev->accept();
            return;
        }
        if (ev->key()==Qt::Key_Delete)
        {
            emit deleteObject_sign(obj);
            ev->accept();
            return;
        }
    }
    QTreeWidget::keyPressEvent(ev);
}

void ObjectTreeView::keyReleaseEvent( QKeyEvent* ev )
{
    QTreeWidget::keyReleaseEvent(ev);
}

void ObjectTreeView::itemOpened ()
{
    if (fwbdebug) qDebug("ObjectTreeView::itemOpened");
    editCurrentObject();
}

void ObjectTreeView::clearLastSelected()
{
    lastSelected = NULL;
}


void ObjectTreeView::resetSelection()
{
    if (lastSelected)
    {
        if (fwbdebug)
            qDebug() << "ObjectTreeView::resetSelection :: lastSelected="
                     << lastSelected->text(0);

        setCurrentItem(lastSelected);
        lastSelected->setSelected(true);
    }
}

void ObjectTreeView::itemSelectionChanged()
{
    if (fwbdebug)
        qDebug("ObjectTreeView::itemSelectionChanged selectionFrozen=%d",
               selectionFrozen);

    if (selectionFrozen) return;

/* in extended selection mode there may be several selected items */

    selectedObjects.clear();

    QList<QTreeWidgetItem*> selected = selectedItems();
    QList<QTreeWidgetItem*>::Iterator it;
    for (it=selected.begin(); it!=selected.end(); it++)
    {
        QTreeWidgetItem *itm = (*it);
        ObjectTreeViewItem *otvi = dynamic_cast<ObjectTreeViewItem*>(itm);

        selectedObjects.push_back(otvi->getFWObject());

        if (fwbdebug) qDebug(
            "ObjectTreeView::selectionChanged: selected otvi=%p object %s",
            otvi, otvi->getFWObject()->getName().c_str());
    }

    if (fwbdebug)
        qDebug("ObjectTreeView::itemSelectionChanged completed");
/* now list  selectedObjects   holds all selected items */
}

bool ObjectTreeView::isSelected(FWObject* obj)
{
    for (vector<FWObject*>::iterator i=selectedObjects.begin();
         i!=selectedObjects.end(); ++i)
    {
        if ( (*i)==obj)  return true;
    }
    return false;
}

int  ObjectTreeView::getNumSelected()
{
    return selectedObjects.size();
}

void ObjectTreeView::updateAfterPrefEdit()
{
     setFont(st->getTreeFont());
}

void ObjectTreeView::ExpandTreeItems(const set<int> &ids)
{
    QTreeWidgetItemIterator it(this);
    while ( *it )
    {
        QTreeWidgetItem *itm = *it;
        ObjectTreeViewItem *otvi=dynamic_cast<ObjectTreeViewItem*>(itm);
        FWObject *obj = otvi->getFWObject();
        if (ids.count(obj->getId()))
            itm->setExpanded(true);
        ++it;
    }
}

void ObjectTreeView::showOrHideAttributesColumn()
{
    if (st->getBool("UI/ShowObjectsAttributesInTree"))
        setColumnCount(2);
    else
        setColumnCount(1);
}

QSet<QTreeWidgetItem*> ObjectTreeView::resolveChildren(QTreeWidgetItem *parent)
{
    QSet<QTreeWidgetItem*> children;
    children.insert(parent);
    if (parent->childCount() == 0) return children;
    for (int i=0; i<parent->childCount(); i++)
        children.unite(resolveChildren(parent->child(i)));
    return children;
}

QSet<QTreeWidgetItem*> ObjectTreeView::resolveParents(QTreeWidgetItem *child)
{
    QSet<QTreeWidgetItem*> parents;
    parents.insert(child);
    if (child->parent() == NULL) return parents;
    parents.unite(resolveParents(child->parent()));
    return parents;
}

void ObjectTreeView::updateFilter()
{
    if (filter.isEmpty()) return;
    setFilter(filter);
}

void ObjectTreeView::setFilter(QString text)
{
    if (fwbdebug)
        qDebug() << "ObjectTreeView::setFilter " << text;

    if (filter.isEmpty() && text.isEmpty()) return;
    filter = text;
    QSet<QTreeWidgetItem *> items = this->findItems(text, Qt::MatchContains|Qt::MatchRecursive, 0).toSet();
    QSet<QTreeWidgetItem *> children, parents;
    QSet<QTreeWidgetItem *>::iterator iter;
    for(iter=items.begin(); iter!=items.end(); iter++)
    {
        children.unite(resolveChildren((*iter)));
        parents.unite(resolveParents((*iter)));
    }
    items.unite(children);
    items.unite(parents);
    QTreeWidgetItemIterator witer(this);
    while (*witer)
    {
        if (items.contains((*witer))) (*witer)->setHidden(false);
        else (*witer)->setHidden(true);
        witer++;
    }
    if (!text.isEmpty()) this->expandAll();
}
