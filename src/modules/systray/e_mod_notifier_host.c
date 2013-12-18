#include "e_mod_notifier_host_private.h"

#define WATCHER_BUS "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define WATCHER_IFACE "org.kde.StatusNotifierWatcher"

#define ITEM_IFACE "org.kde.StatusNotifierItem"

const char *Category_Names[] = {
   "unknown", "SystemServices", NULL
};

const char *Status_Names[] = {
   "unknown", "Active", "Passive", "NeedsAttention", NULL
};

static Context_Notifier_Host *ctx = NULL;

void
systray_notifier_item_free(Notifier_Item *item)
{
   Eldbus_Object *obj;
   Eldbus_Signal_Handler *sig;
   Instance_Notifier_Host *host_inst;
   EINA_INLIST_FOREACH(ctx->instances, host_inst)
     {
        Notifier_Item_Icon *ii;
        EINA_INLIST_FOREACH(host_inst->ii_list, ii)
          {
             if (ii->item == item)
               break;
          }
        if (!ii)
          continue;
        host_inst->ii_list = eina_inlist_remove(host_inst->ii_list,
                                                EINA_INLIST_GET(ii));
        evas_object_del(ii->icon);
        free(ii);
        systray_size_updated(host_inst->inst);
     }
   if (item->menu_path)
     e_dbusmenu_unload(item->menu_data);
   eina_stringshare_del(item->bus_id);
   eina_stringshare_del(item->path);
   if (item->attention_icon_name)
     eina_stringshare_del(item->attention_icon_name);
   if (item->icon_name)
     eina_stringshare_del(item->icon_name);
   if (item->icon_path)
     eina_stringshare_del(item->icon_path);
   if (item->id)
     eina_stringshare_del(item->id);
   if (item->menu_path)
     eina_stringshare_del(item->menu_path);
   if (item->title)
     eina_stringshare_del(item->title);
   EINA_LIST_FREE(item->signals, sig)
     eldbus_signal_handler_del(sig);
   obj = eldbus_proxy_object_get(item->proxy);
   eldbus_proxy_unref(item->proxy);
   eldbus_object_unref(obj);
   ctx->item_list = eina_inlist_remove(ctx->item_list, EINA_INLIST_GET(item));
   free(item);
}

static void
image_load(const char *name, const char *path, Evas_Object *image)
{
   if (path && strlen(path))
     {
        char buf[1024];
        sprintf(buf, "%s/%s", path, name);
        if (!e_icon_file_set(image, buf))
          e_util_icon_theme_set(image, "dialog-error");
        return;
     }
   if (!e_util_icon_theme_set(image, name))
     e_util_icon_theme_set(image, "dialog-error");
}

static void
_sub_item_clicked_cb(void *data, E_Menu *m __UNUSED__, E_Menu_Item *mi __UNUSED__)
{
   E_DBusMenu_Item *item = data;
   e_dbusmenu_event_send(item, E_DBUSMENU_ITEM_EVENT_CLICKED);
}

static void
_menu_post_deactivate(void *data, E_Menu *m)
{
   Eina_List *iter;
   E_Menu_Item *mi;
   E_Gadcon *gadcon = data;

   if (gadcon)
     e_gadcon_locked_set(gadcon, 0);
   EINA_LIST_FOREACH(m->items, iter, mi)
     {
        if (mi->submenu)
          e_menu_deactivate(mi->submenu);
     }
   e_object_del(E_OBJECT(m));
}

static E_Menu *
_item_submenu_new(E_DBusMenu_Item *item, E_Menu_Item *mi)
{
   E_DBusMenu_Item *child;
   E_Menu *m;
   E_Menu_Item *submi;

   m = e_menu_new();
   e_menu_post_deactivate_callback_set(m, _menu_post_deactivate, NULL);
   if (mi)
     e_menu_item_submenu_set(mi, m);

   EINA_INLIST_FOREACH(item->sub_items, child)
     {
        if (!child->visible) continue;
        submi = e_menu_item_new(m);
        if (child->type == E_DBUSMENU_ITEM_TYPE_SEPARATOR)
          e_menu_item_separator_set(submi, 1);
        else
          {
             e_menu_item_label_set(submi, child->label);
             e_menu_item_callback_set(submi, _sub_item_clicked_cb, child);
             if (!child->enabled) e_menu_item_disabled_set(submi, 1);
             if (child->toggle_type == E_DBUSMENU_ITEM_TOGGLE_TYPE_CHECKMARK)
               e_menu_item_check_set(submi, 1);
             else if (child->toggle_type == E_DBUSMENU_ITEM_TOGGLE_TYPE_RADIO)
               e_menu_item_radio_set(submi, 1);
             if (child->toggle_type)
               e_menu_item_toggle_set(submi, child->toggle_state);
             if (eina_inlist_count(child->sub_items))
               _item_submenu_new(child, submi);
             e_util_menu_item_theme_icon_set(submi, child->icon_name);
          }
     }
   return m;
}

static void
_item_menu_new(Notifier_Item_Icon *ii)
{
   E_DBusMenu_Item *root_item;
   E_Menu *m;
   E_Zone *zone;
   int x, y;
   E_Gadcon *gadcon = evas_object_data_get(ii->icon, "gadcon");
   EINA_SAFETY_ON_NULL_RETURN(gadcon);
   if (!ii->item->dbus_item) return;
   root_item = ii->item->dbus_item;
   EINA_SAFETY_ON_FALSE_RETURN(root_item->is_submenu);

   m = _item_submenu_new(root_item, NULL);
   e_gadcon_locked_set(gadcon, 1);
   e_menu_post_deactivate_callback_set(m, _menu_post_deactivate, gadcon);

   zone = e_util_zone_current_get(e_manager_current_get());
   ecore_x_pointer_xy_get(zone->container->win, &x, &y);
   e_menu_activate_mouse(m, zone, x, y, 1, 1, E_MENU_POP_DIRECTION_DOWN,
                         ecore_x_current_time_get());
}

void
_clicked_item_cb(void *data, Evas *evas __UNUSED__, Evas_Object *obj __UNUSED__, void *event)
{
   Notifier_Item_Icon *ii = data;
   Evas_Event_Mouse_Down *ev = event;

   if (ev->button != 1) return;
   _item_menu_new(ii);
}

void
systray_notifier_update_menu(void *data, E_DBusMenu_Item *new_root_item)
{
   Notifier_Item *item = data;
   item->dbus_item = new_root_item;
}

static void
image_scale(Instance_Notifier_Host *notifier_inst, Notifier_Item_Icon *ii)
{
   Evas_Coord sz;
   switch (systray_gadcon_get(notifier_inst->inst)->orient)
     {
      case E_GADCON_ORIENT_HORIZ:
      case E_GADCON_ORIENT_TOP:
      case E_GADCON_ORIENT_BOTTOM:
      case E_GADCON_ORIENT_CORNER_TL:
      case E_GADCON_ORIENT_CORNER_TR:
      case E_GADCON_ORIENT_CORNER_BL:
      case E_GADCON_ORIENT_CORNER_BR:
        sz = systray_gadcon_get(notifier_inst->inst)->shelf->h;
        break;

      case E_GADCON_ORIENT_VERT:
      case E_GADCON_ORIENT_LEFT:
      case E_GADCON_ORIENT_RIGHT:
      case E_GADCON_ORIENT_CORNER_LT:
      case E_GADCON_ORIENT_CORNER_RT:
      case E_GADCON_ORIENT_CORNER_LB:
      case E_GADCON_ORIENT_CORNER_RB:
      default:
        sz = systray_gadcon_get(notifier_inst->inst)->shelf->w;
        break;
     }
   sz = sz - 5;
   evas_object_resize(ii->icon, sz, sz);
}

static void
_systray_notifier_inst_item_update(Instance_Notifier_Host *host_inst, Notifier_Item *item, Eina_Bool search)
{
   Notifier_Item_Icon *ii = NULL;
   if (!search)
     goto jump_search;
   EINA_INLIST_FOREACH(host_inst->ii_list, ii)
     {
        if (ii->item == item)
          break;
     }
jump_search:
   if (!ii)
     {
        ii = calloc(1, sizeof(Notifier_Item_Icon));
        ii->item = item;
        host_inst->ii_list = eina_inlist_append(host_inst->ii_list,
                                                      EINA_INLIST_GET(ii));
     }

   if (!ii->icon)
     {
        ii->icon = e_icon_add(evas_object_evas_get(host_inst->edje));
        EINA_SAFETY_ON_NULL_RETURN(ii->icon);
        image_scale(host_inst, ii);
        evas_object_data_set(ii->icon, "gadcon", host_inst->gadcon);
        evas_object_event_callback_add(ii->icon, EVAS_CALLBACK_MOUSE_DOWN,
                                       _clicked_item_cb, ii);
     }
   switch (item->status)
     {
      case STATUS_ACTIVE:
        {
           image_load(item->icon_name, item->icon_path, ii->icon);
           if (!evas_object_visible_get(ii->icon))
             {
                systray_edje_box_append(host_inst->inst, ii->icon);
                evas_object_show(ii->icon);
             }
           break;
        }
      case STATUS_PASSIVE:
        {
           if (evas_object_visible_get(ii->icon))
             {
                systray_edje_box_remove(host_inst->inst, ii->icon);
                evas_object_hide(ii->icon);
             }
           break;
        }
      case STATUS_ATTENTION:
        {
           image_load(item->attention_icon_name, item->icon_path, ii->icon);
           if (!evas_object_visible_get(ii->icon))
             {
                systray_edje_box_append(host_inst->inst, ii->icon);
                evas_object_show(ii->icon);
             }
           break;
        }
      default:
        {
           WRN("unhandled status");
           break;
        }
     }
   systray_size_updated(host_inst->inst);
}

void
systray_notifier_item_update(Notifier_Item *item)
{
   Instance_Notifier_Host *inst;
   EINA_INLIST_FOREACH(ctx->instances, inst)
     _systray_notifier_inst_item_update(inst, item, EINA_TRUE);
}

Instance_Notifier_Host *
systray_notifier_host_new(Instance *inst, E_Gadcon *gadcon)
{
   Instance_Notifier_Host *host_inst = NULL;
   Notifier_Item *item;
   host_inst = calloc(1, sizeof(Instance_Notifier_Host));
   EINA_SAFETY_ON_NULL_RETURN_VAL(host_inst, NULL);
   host_inst->inst = inst;
   host_inst->edje = systray_edje_get(inst);
   host_inst->gadcon = gadcon;
   ctx->instances = eina_inlist_append(ctx->instances, EINA_INLIST_GET(host_inst));

   EINA_INLIST_FOREACH(ctx->item_list, item)
     _systray_notifier_inst_item_update(host_inst, item, EINA_FALSE);

   return host_inst;
}

void
systray_notifier_host_free(Instance_Notifier_Host *notifier)
{
   while (notifier->ii_list)
     {
        Notifier_Item_Icon *ii = EINA_INLIST_CONTAINER_GET(notifier->ii_list, Notifier_Item_Icon);
        notifier->ii_list = eina_inlist_remove(notifier->ii_list,
                                               notifier->ii_list);
        free(ii);
     }
   ctx->instances = eina_inlist_remove(ctx->instances, EINA_INLIST_GET(notifier));
   free(notifier);
}

void
systray_notifier_host_init(void)
{
   ctx = calloc(1, sizeof(Context_Notifier_Host));
   EINA_SAFETY_ON_NULL_RETURN(ctx);
   systray_notifier_dbus_init(ctx);
}

void
systray_notifier_host_shutdown(void)
{
   Eldbus_Pending *p;
   
   EINA_LIST_FREE(ctx->pending, p) eldbus_pending_cancel(p);
   systray_notifier_dbus_shutdown(ctx);
   free(ctx);
   ctx = NULL;
}
