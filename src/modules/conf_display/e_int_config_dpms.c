#include "e.h"

static void *_create_data(E_Config_Dialog *cfd);
static void _free_data(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata);
static int  _advanced_check_changed(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata);
static int  _advanced_apply_data(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata);
static Evas_Object  *_advanced_create_widgets(E_Config_Dialog *cfd, Evas *evas,
					      E_Config_Dialog_Data *cfdata);
static void _cb_disable(void *data, Evas_Object *obj);
static void _cb_ask_presentation_changed(void *data, Evas_Object *obj);

struct _E_Config_Dialog_Data
{
   E_Config_Dialog *cfd;

   Evas_Object *backlight_slider_idle;
   Evas_Object *backlight_slider_fade;

   char *bl_dev;
   
   int enable_idle_dim;

   double backlight_normal;
   double backlight_dim;
   double backlight_timeout;
   double backlight_transition;

   int ask_presentation;
   double ask_presentation_timeout;

   Eina_List *disable_list;

   struct 
     {
        Evas_Object *ask_presentation_slider;
     } gui;
};

E_Config_Dialog *
e_int_config_dpms(Evas_Object *parent EINA_UNUSED, const char *params EINA_UNUSED)
{
   E_Config_Dialog *cfd;
   E_Config_Dialog_View *v;

   if (e_config_dialog_find("E", "screen/power_management"))
     return NULL;

   v = E_NEW(E_Config_Dialog_View, 1);

   v->create_cfdata = _create_data;
   v->free_cfdata = _free_data;
   v->basic.apply_cfdata = _advanced_apply_data;
   v->basic.create_widgets = _advanced_create_widgets;
   v->basic.check_changed = _advanced_check_changed;
   v->override_auto_apply = 1;

   cfd = e_config_dialog_new(NULL, _("Backlight Settings"), "E",
			     "screen/power_management", "preferences-system-power-management",
			     0, v, NULL);
   return cfd;
}

static void
_fill_data(E_Config_Dialog_Data *cfdata)
{
   cfdata->backlight_normal = e_config->backlight.normal * 100.0;
   cfdata->backlight_dim = e_config->backlight.dim * 100.0;
   cfdata->backlight_transition = e_config->backlight.transition;
   cfdata->enable_idle_dim = e_config->backlight.idle_dim;
   cfdata->backlight_timeout = e_config->backlight.timer;
   cfdata->ask_presentation = e_config->screensaver_ask_presentation;
   cfdata->ask_presentation_timeout = e_config->screensaver_ask_presentation_timeout;
}

static void *
_create_data(E_Config_Dialog *cfd)
{
   E_Config_Dialog_Data *cfdata;

   cfdata = E_NEW(E_Config_Dialog_Data, 1);
   cfdata->cfd = cfd;

   _fill_data(cfdata);
   return cfdata;
}

static void
_free_data(E_Config_Dialog *cfd EINA_UNUSED, E_Config_Dialog_Data *cfdata)
{
   E_FREE(cfdata);
}

static int
_apply_data(E_Config_Dialog *cfd EINA_UNUSED, E_Config_Dialog_Data *cfdata)
{
   e_config->backlight.normal = cfdata->backlight_normal / 100.0;
   e_config->backlight.dim = cfdata->backlight_dim / 100.0;
   e_config->backlight.transition = cfdata->backlight_transition;
   e_config->backlight.timer = lround(cfdata->backlight_timeout);
   e_config->backlight.idle_dim = cfdata->enable_idle_dim;
   e_config->screensaver_ask_presentation = cfdata->ask_presentation;
   e_config->screensaver_ask_presentation_timeout = cfdata->ask_presentation_timeout;

   e_backlight_mode_set(NULL, E_BACKLIGHT_MODE_NORMAL);
   e_backlight_level_set(NULL, e_config->backlight.normal, -1.0);
   
   if ((e_config->backlight.idle_dim) &&
       (e_config->backlight.timer > (e_config->screensaver_timeout)))
     {
        e_config->screensaver_timeout = e_config->backlight.timer;
        e_config->dpms_standby_timeout = e_config->screensaver_timeout;
        e_config->dpms_suspend_timeout = e_config->screensaver_timeout;
        e_config->dpms_off_timeout = e_config->screensaver_timeout;
     }
   e_screensaver_update();
   e_dpms_update();
   e_backlight_update();
   e_config_save_queue();
   return 1;
}

/* advanced window */
static int
_advanced_check_changed(E_Config_Dialog *cfd EINA_UNUSED, E_Config_Dialog_Data *cfdata)
{
   // set state from saved config
   e_widget_disabled_set(cfdata->backlight_slider_idle, !cfdata->enable_idle_dim);
   e_widget_disabled_set(cfdata->backlight_slider_fade, !cfdata->enable_idle_dim);

   return (!EINA_DBL_EQ(e_config->backlight.normal * 100.0, cfdata->backlight_normal)) ||
          (!EINA_DBL_EQ(e_config->backlight.dim * 100.0, cfdata->backlight_dim)) ||
          (!EINA_DBL_EQ(e_config->backlight.transition, cfdata->backlight_transition)) ||
          (!EINA_DBL_EQ(e_config->backlight.timer, cfdata->backlight_timeout)) ||
          (e_config->backlight.idle_dim != cfdata->enable_idle_dim) ||
          (e_config->screensaver_ask_presentation != cfdata->ask_presentation) ||
          (!EINA_DBL_EQ(e_config->screensaver_ask_presentation_timeout, cfdata->ask_presentation_timeout));
}

static int
_advanced_apply_data(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata)
{
   _apply_data(cfd, cfdata);
   return 1;
}

static Evas_Object *
_advanced_create_widgets(E_Config_Dialog *cfd EINA_UNUSED, Evas *evas, E_Config_Dialog_Data *cfdata)
{
   Evas_Object *otb, *o, *ob;
   Eina_List *devs, *l;
   const char *s, *label;

   otb = e_widget_toolbook_add(evas, (24 * e_scale), (24 * e_scale));
   
   /* Dimming */
   o = e_widget_list_add(evas, 0, 0);
/*
   {
      char buf[32];
      switch (e_config->backlight.mode)
        {
         case 0:
           snprintf(buf, sizeof(buf), "%s: RANDR", _("Mode"));
           break;
         case 1:
           snprintf(buf, sizeof(buf), "%s: EEZE", _("Mode"));
           break;
         default:
           snprintf(buf, sizeof(buf), "%s: NONE", _("Mode"));
        }
      ob = e_widget_label_add(evas, buf);
      e_widget_list_object_append(o, ob, 0, 1, 0.5);
   }
 */
   ob = e_widget_label_add(evas, _("Normal Backlight"));
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   ob = e_widget_slider_add(evas, 1, 0, _("%3.0f"), 0.0, 100.0, 1.0, 0,
			    &(cfdata->backlight_normal), NULL, 100);
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   
   ob = e_widget_label_add(evas, _("Dim Backlight"));
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   ob = e_widget_slider_add(evas, 1, 0, _("%3.0f"), 0.0, 100.0, 1.0, 0,
			    &(cfdata->backlight_dim), NULL, 100);
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   
   ob = e_widget_check_add(evas, _("Idle Fade Time"), &(cfdata->enable_idle_dim));
   e_widget_on_change_hook_set(ob, _cb_disable, cfdata);
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   ob = e_widget_slider_add(evas, 1, 0, _("%1.0f second(s)"), 5.0, 300.0, 1.0, 0,
			    &(cfdata->backlight_timeout), NULL, 100);
   cfdata->backlight_slider_idle = ob;
   e_widget_disabled_set(ob, !cfdata->enable_idle_dim); // set state from saved config
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   
   ob = e_widget_label_add(evas, _("Fade Time"));
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   ob = e_widget_slider_add(evas, 1, 0, _("%1.1f second(s)"), 0.0, 5.0, 0.1, 0,
			    &(cfdata->backlight_transition), NULL, 100);
   cfdata->backlight_slider_fade = ob;
   e_widget_disabled_set(ob, !cfdata->enable_idle_dim); // set state from saved config
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   
   devs = (Eina_List *)e_backlight_devices_get();
   if ((devs) && (eina_list_count(devs) > 1))
     {
        int sel = -1, i = 0;
        
        ob = e_widget_ilist_add(evas, 16, 16, NULL);
        e_widget_size_min_set(ob, 100, 100);
        e_widget_list_object_append(o, ob, 1, 1, 0.5);
        EINA_LIST_FOREACH(devs, l, s)
          {
             label = strchr(s, '/');
             if (!label) label = s;
             else label++;
             e_widget_ilist_append(ob, NULL, label, NULL, NULL, s);
             if ((e_config->backlight.sysdev) &&
                 (!strcmp(e_config->backlight.sysdev, s)))
               sel = i;
             i++;
          }
        e_widget_ilist_go(ob);
        if (sel >= 0) e_widget_ilist_selected_set(ob, sel);
     }

   e_widget_toolbook_page_append(otb, NULL, _("Dimming"), o, 
                                 1, 0, 1, 0, 0.5, 0.0);

   // FIXME: Disabled until someone want's to cleanup that screensaver code...   
   /* Presentation */
   /*
   o = e_widget_list_add(evas, 0, 0);
   ob = e_widget_check_add(evas, _("Suggest if deactivated before"), 
                           &(cfdata->ask_presentation));
   e_widget_on_change_hook_set(ob, _cb_ask_presentation_changed, cfdata);
   cfdata->disable_list = eina_list_append(cfdata->disable_list, ob);
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   ob = e_widget_slider_add(evas, 1, 0, _("%1.0f seconds"),
			    1.0, 300.0, 10.0, 0,
			    &(cfdata->ask_presentation_timeout), NULL, 100);
   cfdata->gui.ask_presentation_slider = ob;
   cfdata->disable_list = eina_list_append(cfdata->disable_list, ob);
   e_widget_list_object_append(o, ob, 1, 1, 0.5);
   e_widget_toolbook_page_append(otb, NULL, _("Presentation"), o,
                                 1, 0, 1, 0, 0.5, 0.0);
   */
   
   e_widget_toolbook_page_show(otb, 0);

   // handler for enable/disable widget array
   _cb_disable(cfdata, NULL);

   return otb;
}

static void
_cb_disable(void *data, Evas_Object *obj EINA_UNUSED)
{
   E_Config_Dialog_Data *cfdata;
   const Eina_List *l;
   Evas_Object *o;

   if (!(cfdata = data)) return;
   EINA_LIST_FOREACH(cfdata->disable_list, l, o)
     e_widget_disabled_set(o, !cfdata->enable_idle_dim);

   _cb_ask_presentation_changed(cfdata, NULL);
}

static void
_cb_ask_presentation_changed(void *data, Evas_Object *obj EINA_UNUSED)
{
   E_Config_Dialog_Data *cfdata;
   Eina_Bool disable;

   if (!(cfdata = data)) return;
   disable = ((!cfdata->enable_idle_dim) || (!cfdata->ask_presentation));
   e_widget_disabled_set(cfdata->gui.ask_presentation_slider, disable);
}
