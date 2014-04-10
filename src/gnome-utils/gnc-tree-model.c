/* 
 * gnc-tree-model.c -- base implementation for a tree model in
 *                     Gnucash.  This only implements the object, not
 *                     the model interface.
 *
 * Copyright (C) 2005 David Hampton <hampton@employees.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652
 * Boston, MA  02111-1307,  USA       gnu@gnu.org
 */

#include "config.h"
#include <string.h>
#include <gtk/gtk.h>

#include "gnc-tree-model.h"

#include "gnc-gobject-utils.h"
#include "gnc-engine.h"
#include "messages.h"

/** Static Globals *******************************************************/
static QofLogModule log_module = GNC_MOD_GUI;

/** Declarations *********************************************************/
static void gnc_tree_model_class_init (GncTreeModelClass *klass);
static void gnc_tree_model_init (GncTreeModel *model, GncTreeModelClass *klass);
static void gnc_tree_model_finalize (GObject *object);
static void gnc_tree_model_destroy (GtkObject *object);

struct GncTreeModelPrivate
{
  gpointer dummy;
};


/************************************************************/
/*               g_object required functions                */
/************************************************************/

static GtkObjectClass *parent_class = NULL;

GType
gnc_tree_model_get_type (void)
{
  static GType gnc_tree_model_type = 0;

  if (gnc_tree_model_type == 0) {
    static const GTypeInfo our_info = {
      sizeof (GncTreeModelClass),          /* class_size */
      NULL,   			           /* base_init */
      NULL,				   /* base_finalize */
      (GClassInitFunc) gnc_tree_model_class_init,
      NULL,				   /* class_finalize */
      NULL,				   /* class_data */
      sizeof (GncTreeModel),	           /* */
      0,				   /* n_preallocs */
      (GInstanceInitFunc) gnc_tree_model_init
    };

    //static const GInterfaceInfo tree_model_info = {
    //  (GInterfaceInitFunc) gnc_tree_model_tree_model_init,
    //  NULL,
    //  NULL
    //};

    /* CAS: I think this should subclass GObject, not GtkObject. */

    gnc_tree_model_type = g_type_register_static (GTK_TYPE_OBJECT,
						  GNC_TREE_MODEL_NAME,
						  &our_info, 0);

    //g_type_add_interface_static (gnc_tree_model_type,
    //				 GTK_TYPE_TREE_MODEL,
    //				 &tree_model_info);
  }

  return gnc_tree_model_type;
}

static void
gnc_tree_model_class_init (GncTreeModelClass *klass)
{
  GObjectClass *o_class;
  GtkObjectClass *object_class;

  parent_class = g_type_class_peek_parent (klass);

  o_class = G_OBJECT_CLASS (klass);
  object_class = GTK_OBJECT_CLASS (klass);

  /* GObject signals */
  o_class->finalize = gnc_tree_model_finalize;

  /* GtkObject signals */
  object_class->destroy = gnc_tree_model_destroy;
}

static void
gnc_tree_model_init (GncTreeModel *model, GncTreeModelClass *klass)
{
  ENTER("model %p", model);
  gnc_gobject_tracking_remember(G_OBJECT(model), G_OBJECT_CLASS(klass));

  model->priv = g_new0 (GncTreeModelPrivate, 1);

  LEAVE(" ");
}

static void
gnc_tree_model_finalize (GObject *object)
{
  ENTER("model %p", object);
  g_return_if_fail (object != NULL);
  g_return_if_fail (GNC_IS_TREE_MODEL (object));

  gnc_gobject_tracking_forget(object);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    (* G_OBJECT_CLASS (parent_class)->finalize) (object);
  LEAVE(" ");
}

static void
gnc_tree_model_destroy (GtkObject *object)
{
  GncTreeModel *model;

  ENTER("model %p", object);
  g_return_if_fail (object != NULL);
  g_return_if_fail (GNC_IS_TREE_MODEL (object));

  model = GNC_TREE_MODEL (object);
  g_free (model->priv);
  model->priv = NULL;

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
  LEAVE(" ");
}