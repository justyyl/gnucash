/********************************************************************\
 * dialog-transfer.c -- transfer dialog for GnuCash                 *
 * Copyright (C) 1999 Linas Vepstas                                 *
 * Copyright (C) 2000 Dave Peticolas                                *
 * Copyright (C) 2000 Herbert Thoma                                 *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
\********************************************************************/

#include "config.h"

#include <gtk/gtk.h>

#include "dialog-transfer.h"
#include "dialog-utils.h"
#include "gnc-amount-edit.h"
#include "gnc-book.h"
#include "gnc-component-manager.h"
#include "gnc-date-edit.h"
#include "gnc-engine.h"
#include "gnc-euro.h"
#include "gnc-exp-parser.h"
#include "gnc-gconf-utils.h"
#include "gnc-gui-query.h"
#include "gnc-pricedb.h"
#include "gnc-tree-view-account.h"
#include "gnc-ui.h"
#include "messages.h"
#include "Transaction.h"
#include "Account.h"


#define DIALOG_TRANSFER_CM_CLASS "dialog-transfer"

#define PRECISION 1000000

typedef enum
{
  XFER_DIALOG_FROM,
  XFER_DIALOG_TO
} XferDirection;


/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_GUI;

struct _xferDialog
{
  GtkWidget * dialog;

  GtkWidget * amount_edit;
  GtkWidget * date_entry;
  GtkWidget * num_entry;
  GtkWidget * description_entry;
  GtkWidget * memo_entry;
  GtkWidget * conv_forward;
  GtkWidget * conv_reverse;

  GtkWidget *	from_window;
  GtkTreeView * from_tree_view;
  gnc_commodity *	from_commodity;
  GtkWidget *	to_window;
  GtkTreeView * to_tree_view;
  gnc_commodity *	to_commodity;

  QuickFill * qf;     /* Quickfill on transfer descriptions, 
                         defaults to matching on the "From" account. */

  XferDirection quickfill;	/* direction match on the account instead. */

  /* stored data for the description quickfill functionality */
  gint desc_start_selection;
  gint desc_end_selection;
  gint desc_cursor_position;
  gboolean desc_didquickfill;

  GtkWidget * transferinfo_label;

  GtkWidget * from_transfer_label;
  GtkWidget * to_transfer_label;

  GtkWidget * from_currency_label;
  GtkWidget * to_currency_label;

  GtkWidget * from_show_button;
  GtkWidget * to_show_button;

  GtkWidget * curr_xfer_table;

  GtkWidget * price_edit;
  GtkWidget * to_amount_edit;

  GtkWidget * price_radio;
  GtkWidget * amount_radio;

  GtkTooltips *tips;

  GNCBook *	book;
  GNCPriceDB *	pricedb;

  /* Where to store the "exchange_rate" at exit (in lieu of
   * creating a transaction)
   */
  gnc_numeric * exch_rate;

  /* Callback funtion to notify of the newly created Transaction */
  gnc_xfer_dialog_cb transaction_cb;
  /* , and its user_data */
  gpointer transaction_user_data;
};

struct _acct_list_item
{
  char *acct_full_name;
  Account *acct;
};
typedef struct _acct_list_item acct_list_item;


/** Prototypes ***************************************************/
static void gnc_xfer_update_to_amount (XferDialog *xferData);
static void gnc_xfer_dialog_update_conv_info(XferDialog *xferData);

static Account *gnc_transfer_dialog_get_selected_account (XferDialog *dialog,
							  XferDirection direction);
static void gnc_transfer_dialog_set_selected_account (XferDialog *dialog,
						      Account *account,
						      XferDirection direction);
void gnc_xfer_dialog_response_cb (GtkDialog *dialog, gint response, gpointer data);
void gnc_xfer_dialog_close_cb(GtkDialog *dialog, gpointer data);

/** Implementations **********************************************/

static gnc_numeric
gnc_xfer_dialog_compute_price (XferDialog *xferData)
{
  gnc_numeric from_amt, to_amt;

  from_amt = gnc_amount_edit_get_amount(GNC_AMOUNT_EDIT(xferData->amount_edit));
  to_amt = gnc_amount_edit_get_amount(GNC_AMOUNT_EDIT(xferData->to_amount_edit));

  return(gnc_numeric_div(to_amt, from_amt, GNC_DENOM_AUTO, GNC_DENOM_REDUCE));
}

/* (maybe) update the price from the pricedb. */
static void
gnc_xfer_dialog_update_price (XferDialog *xferData)
{
  GNCPrice *prc;
  gnc_numeric price;
  Timespec date;
  gnc_commodity *from = xferData->from_commodity;
  gnc_commodity *to = xferData->to_commodity;

  if (!xferData) return;
  if (!xferData->from_commodity || ! xferData->to_commodity) return;
  if (gnc_commodity_equal (xferData->from_commodity, xferData->to_commodity))
    return;
  if (!xferData->pricedb) return;

  /* when do we update, and when do we NOT update? */

  /* XXX: I'm ALWAYS going to update whenver we get called */

  /* grab the price nearest to the DATE out of the pricedb */
  date = gnc_date_edit_get_date_ts (GNC_DATE_EDIT (xferData->date_entry));
  prc = gnc_pricedb_lookup_nearest_in_time (xferData->pricedb,
					    from, to, date);

  if (prc) {
    /* grab the price from the pricedb */
    price = gnc_price_get_value (prc);
    PINFO("Found price: 1 %s = %f %s", gnc_commodity_get_mnemonic(from),
	  gnc_numeric_to_double(price), gnc_commodity_get_mnemonic(to));
  } else {
    prc = gnc_pricedb_lookup_nearest_in_time (xferData->pricedb,
					      to, from, date);
    if (!prc)
      return;
    price = gnc_price_get_value (prc);
    PINFO("Found reverse price: 1 %s = %f %s", gnc_commodity_get_mnemonic(to),
	  gnc_numeric_to_double(price), gnc_commodity_get_mnemonic(from));
    price = gnc_numeric_div (gnc_numeric_create (1, 1), price,
			     GNC_DENOM_AUTO, GNC_DENOM_REDUCE);
  }

  /* and set the price entry */
  gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT (xferData->price_edit), price);

  /* And then update the to_amount */
  gnc_xfer_update_to_amount (xferData);
}

static void
gnc_xfer_dialog_toggle_cb(GtkToggleButton *button, gpointer data)
{
  gnc_tree_view_account_refilter (GNC_TREE_VIEW_ACCOUNT (data));
}

static void
gnc_xfer_dialog_set_price_auto (XferDialog *xferData,
                                gboolean currency_active,
                                const gnc_commodity *from_currency,
                                const gnc_commodity *to_currency)
{
  gnc_numeric from_rate;
  gnc_numeric to_rate;
  gnc_numeric price;

  if (!currency_active)
  {
    GtkEntry *entry;

    gnc_amount_edit_set_amount(GNC_AMOUNT_EDIT(xferData->price_edit),
                               gnc_numeric_zero ());
    entry = GTK_ENTRY(gnc_amount_edit_gtk_entry
                      (GNC_AMOUNT_EDIT(xferData->price_edit)));
    gtk_entry_set_text(entry, "");

    gnc_xfer_update_to_amount (xferData);

    return;
  }

  if (!gnc_is_euro_currency (from_currency) ||
      !gnc_is_euro_currency (to_currency))
    return gnc_xfer_dialog_update_price (xferData);

  from_rate = gnc_euro_currency_get_rate (from_currency);
  to_rate = gnc_euro_currency_get_rate (to_currency);

  if (gnc_numeric_zero_p (from_rate) || gnc_numeric_zero_p (to_rate))
    gnc_xfer_dialog_update_price (xferData);

  price = gnc_numeric_div (to_rate, from_rate, GNC_DENOM_AUTO, GNC_DENOM_REDUCE);

  gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT(xferData->price_edit), price);

  gnc_xfer_update_to_amount (xferData);
}

static void
gnc_xfer_dialog_curr_acct_activate(XferDialog *xferData)
{
  Account *to_account;
  Account *from_account;
  gboolean curr_active;

  from_account = 
    gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_FROM);

  to_account = 
    gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_TO);

  curr_active = (xferData->exch_rate ||
		 ((from_account != NULL) && (to_account != NULL)))
		 && !gnc_commodity_equiv(xferData->from_commodity,
					 xferData->to_commodity);

  gtk_widget_set_sensitive(xferData->curr_xfer_table, curr_active);
  gtk_widget_set_sensitive(xferData->price_edit,
			   curr_active && gtk_toggle_button_get_active
			   (GTK_TOGGLE_BUTTON(xferData->price_radio)));
  gtk_widget_set_sensitive(xferData->to_amount_edit, 
			   curr_active && gtk_toggle_button_get_active
			   (GTK_TOGGLE_BUTTON(xferData->amount_radio)));
  gtk_widget_set_sensitive(xferData->price_radio, curr_active);
  gtk_widget_set_sensitive(xferData->amount_radio, curr_active);

  gnc_xfer_dialog_set_price_auto (xferData, curr_active,
                                  xferData->from_commodity, xferData->to_commodity);
  gnc_xfer_dialog_update_conv_info(xferData);

  if (!curr_active)
  {
    GtkEntry *entry;

    gnc_amount_edit_set_amount(GNC_AMOUNT_EDIT(xferData->to_amount_edit),
                               gnc_numeric_zero ());
    entry = GTK_ENTRY(gnc_amount_edit_gtk_entry
		      (GNC_AMOUNT_EDIT(xferData->to_amount_edit)));
    gtk_entry_set_text(entry, "");
  }
}


static void
price_amount_radio_toggled_cb(GtkToggleButton *togglebutton, gpointer data)
{
  XferDialog *xferData = data;

  gtk_widget_set_sensitive(xferData->price_edit, gtk_toggle_button_get_active
			   (GTK_TOGGLE_BUTTON(xferData->price_radio)));
  gtk_widget_set_sensitive(xferData->to_amount_edit,
			   gtk_toggle_button_get_active
			   (GTK_TOGGLE_BUTTON(xferData->amount_radio)));
}


/* Reload the xferDialog quickfill with the descriptions
 * from the currently selected from account.  Note that this
 * doesn't use the initial account passed into gnc_xfer_dialog,
 * because that's NULL if no account is selected in the main
 * account window tree view.
 */
static void
gnc_xfer_dialog_reload_quickfill( XferDialog *xferData )
{
  GList *splitlist, *node;
  Split *split;
  Transaction *trans;
  Account *account;

  account = gnc_transfer_dialog_get_selected_account (xferData, xferData->quickfill);

  /* get a new QuickFill to use */
  gnc_quickfill_destroy( xferData->qf );
  xferData->qf = gnc_quickfill_new();

  splitlist = xaccAccountGetSplitList( account );

  for( node = splitlist; node; node = node->next )
  {
    split = node->data;
    trans = xaccSplitGetParent( split );
    gnc_quickfill_insert( xferData->qf,
                          xaccTransGetDescription (trans), QUICKFILL_LIFO);
  }
}


static void
gnc_xfer_dialog_from_tree_selection_changed_cb (GtkTreeSelection *selection,
						gpointer data)
{
  XferDialog *xferData = data;
  GNCPrintAmountInfo print_info;
  gnc_commodity *commodity;
  Account *account;

  account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_FROM);
  commodity = xaccAccountGetCommodity(account);
  gtk_label_set_text(GTK_LABEL(xferData->from_currency_label), 
		     gnc_commodity_get_printname(commodity));

  xferData->from_commodity = commodity;

  gnc_xfer_dialog_curr_acct_activate(xferData);

  print_info = gnc_account_print_info (account, FALSE);

  gnc_amount_edit_set_print_info (GNC_AMOUNT_EDIT (xferData->amount_edit),
                                  print_info);
  gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT (xferData->amount_edit),
                                xaccAccountGetCommoditySCU (account));

  /* Reload the xferDialog quickfill if it is based on the from account */
  if (xferData->quickfill == XFER_DIALOG_FROM)
    gnc_xfer_dialog_reload_quickfill(xferData);
}


static void
gnc_xfer_dialog_to_tree_selection_changed_cb (GtkTreeSelection *selection, gpointer data)
{
  XferDialog *xferData = data;
  GNCPrintAmountInfo print_info;
  gnc_commodity *commodity;
  Account *account;

  account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_TO);
  commodity = xaccAccountGetCommodity(account);
  gtk_label_set_text(GTK_LABEL(xferData->to_currency_label),
		     gnc_commodity_get_printname(commodity));

  xferData->to_commodity = commodity;

  gnc_xfer_dialog_curr_acct_activate(xferData);

  print_info = gnc_account_print_info (account, FALSE);

  gnc_amount_edit_set_print_info (GNC_AMOUNT_EDIT (xferData->to_amount_edit),
                                  print_info);
  gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT (xferData->to_amount_edit),
                                xaccAccountGetCommoditySCU (account));

  /* Reload the xferDialog quickfill if it is based on the to account */
  if (xferData->quickfill == XFER_DIALOG_TO)
    gnc_xfer_dialog_reload_quickfill(xferData);
}

static gboolean
gnc_xfer_dialog_show_inc_exp_visible_cb (Account *account,
					 gpointer data)
{
  GtkCheckButton *show_button;
  GNCAccountType type;

  g_return_val_if_fail (GTK_IS_CHECK_BUTTON (data), FALSE);

  show_button = GTK_CHECK_BUTTON (data);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (show_button))) {
    return TRUE;
  }

  type = xaccAccountGetType(account);
  return ((type != INCOME) && (type != EXPENSE)); 
}

static void
gnc_xfer_dialog_fill_tree_view(XferDialog *xferData,
			       XferDirection direction)
{
  GtkTreeView *tree_view;
  const char *show_inc_exp_message = _("Show the income and expense accounts");
  GtkWidget *scroll_win, *box;
  GtkWidget *button;
  GtkTreeSelection *selection;
  gboolean  use_accounting_labels;

  use_accounting_labels = gnc_gconf_get_bool(GCONF_GENERAL,
					     KEY_ACCOUNTING_LABELS, NULL);

  /* In "normal" mode (non accounting terms) the account where the
   * money comes from is displayed on the left side and the account
   * where the money gets transferred to is displayed on the right
   * side. In accounting terms the "from" account is called the
   * "credit" account ("Haben" in german) and the "to" account is
   * called "debit" account ("Soll" in german). Accountants told me
   * that they always want the credit account on the right side
   * and the debit on the left side (like the debit and credit
   * columns in the register window). So reverse from and to account
   * trees when in "accountant" mode. -- Herbert Thoma, 2004-01-18
   */
  if(use_accounting_labels) {
    box = gnc_glade_lookup_widget (xferData->dialog,
				   (direction == XFER_DIALOG_TO) ?
				   "left_tree_box" : "right_tree_box");
    button = gnc_glade_lookup_widget (xferData->dialog,
				      (direction == XFER_DIALOG_TO) ?
				      "left_show_button" : "right_show_button");
    scroll_win = gnc_glade_lookup_widget (xferData->dialog,
					  (direction == XFER_DIALOG_TO) ?
					  "left_trans_window" : "right_trans_window");
  }
  else {
    box = gnc_glade_lookup_widget (xferData->dialog,
				   (direction == XFER_DIALOG_TO) ?
				   "right_tree_box" : "left_tree_box");
    button = gnc_glade_lookup_widget (xferData->dialog,
				      (direction == XFER_DIALOG_TO) ?
				      "right_show_button" : "left_show_button");
    scroll_win = gnc_glade_lookup_widget (xferData->dialog,
					  (direction == XFER_DIALOG_TO) ?
					  "right_trans_window" : "left_trans_window");
  }

  tree_view = GTK_TREE_VIEW(gnc_tree_view_account_new(FALSE));
  gtk_container_add(GTK_CONTAINER(box), GTK_WIDGET(tree_view));
  gnc_tree_view_account_set_filter (GNC_TREE_VIEW_ACCOUNT (tree_view),
				    gnc_xfer_dialog_show_inc_exp_visible_cb,
				    button, /* user data */
				    NULL    /* destroy callback */);
 /* Have to force the filter once. Alt is to show income/expense by default. */
  gnc_tree_view_account_refilter (GNC_TREE_VIEW_ACCOUNT (tree_view));
  gtk_widget_show(GTK_WIDGET(tree_view));

  selection = gtk_tree_view_get_selection (tree_view);
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
  gtk_tooltips_set_tip (xferData->tips, GTK_WIDGET (button), show_inc_exp_message, NULL);

  if (direction == XFER_DIALOG_TO) {
    xferData->to_tree_view = tree_view;
    xferData->to_window = scroll_win;
    xferData->to_show_button = GTK_WIDGET (button);
    g_signal_connect (G_OBJECT (selection), "changed",
		      G_CALLBACK (gnc_xfer_dialog_to_tree_selection_changed_cb), xferData);
  } else {
    xferData->from_tree_view = tree_view;
    xferData->from_window = scroll_win;
    xferData->from_show_button = GTK_WIDGET (button);
    g_signal_connect (G_OBJECT (selection), "changed",
		      G_CALLBACK (gnc_xfer_dialog_from_tree_selection_changed_cb), xferData);
  }
  g_signal_connect (G_OBJECT (button), "toggled",
		    G_CALLBACK (gnc_xfer_dialog_toggle_cb), tree_view);
}


static void
gnc_parse_error_dialog (XferDialog *xferData, const char *error_string)
{
  const char * parse_error_string;

  parse_error_string = gnc_exp_parser_error_string ();
  if (parse_error_string == NULL)
    parse_error_string = "";

  if (error_string == NULL)
    error_string = "";

  gnc_error_dialog (xferData->dialog,
		    "%s\n\n%s: %s.",
		    error_string, _("Error"),
		    parse_error_string);
}

/*** Callbacks for description quickfill. ***/

/* gnc_xfer_dialog_quickfill will update the fields of the dialog
 * based on the contents of the Description entry.  Returns TRUE
 * if the fields were updated, or FALSE if the fields were already
 * updated or if the Description couldn't be matched and no updates
 * were made.
 */
static gboolean
gnc_xfer_dialog_quickfill( XferDialog *xferData )
{
  const char *desc;
  Account *match_account;  /* the matched text was from this account */
  Split *split;            /* the split to autocomplete from */
  Split *other = NULL;     /* the other split of the transaction */
  Account *other_acct = NULL;   /* the Account of the other split */
  gboolean changed = FALSE;

  ENTER("xferData=%p", xferData);
  if( !xferData ) {
    LEAVE("bad args");
    return( FALSE );
  }

  match_account = gnc_transfer_dialog_get_selected_account (xferData, xferData->quickfill);

  desc = gtk_entry_get_text( GTK_ENTRY(xferData->description_entry) );

  if( !desc || desc[0] == '\0' )   /* no description to match */
    return( FALSE );

  split = xaccAccountFindSplitByDesc( match_account, desc );

  if( !split ) {
    LEAVE("split not found");
    return( FALSE );
  }
  DEBUG("split=%p", split);

  /* Now update any blank fields of the transfer dialog with
   * the memo and amount from the split, and the description
   * we were passed (assumed to match the split's transaction).
   */

  if( gnc_numeric_zero_p(
           gnc_amount_edit_get_amount(GNC_AMOUNT_EDIT(xferData->amount_edit))))
  {
    gnc_numeric amt;
    DEBUG("updating amount");
    amt = xaccSplitGetValue( split );

    /* If we've matched a previous transfer, it will appear
     * to be negative in the from account.
     * Need to swap the sign in order for this value
     * to be posted as a withdrawal from the "from" account.
     */
    if( gnc_numeric_negative_p( amt ) )
      amt = gnc_numeric_neg( amt );

    gnc_amount_edit_set_amount( GNC_AMOUNT_EDIT(xferData->amount_edit), amt );
    changed = TRUE;
  }

  if( !safe_strcmp(gtk_entry_get_text(GTK_ENTRY(xferData->memo_entry)),"" ))
  {
    DEBUG("updating memo");
    gtk_entry_set_text( GTK_ENTRY(xferData->memo_entry),
                        xaccSplitGetMemo( split ) );
    changed = TRUE;
  }

  /* Since we're quickfilling off of one account (either from or to)
   * that account must be the account of the matched split.
   * Find the other account from the other split,
   * and select that account in the appropriate account tree.
   */
  if( ( other = xaccSplitGetOtherSplit( split ) ) &&
      ( other_acct = xaccSplitGetAccount( other ) ) )
  {
    GNCAccountType other_type;
    GtkWidget *other_button;
    
    DEBUG("updating other split");
    if (xferData->quickfill == XFER_DIALOG_FROM) {
      other_button = xferData->from_show_button;
    }
    else
    {
      other_button = xferData->to_show_button;
    }

    other_type = xaccAccountGetType(other_acct);

    /* Don't want to deactivate the button just because this
     * isn't an income or expense account
     */
    if( (other_type == EXPENSE) || (other_type == INCOME) )
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other_button), TRUE);

    gnc_transfer_dialog_set_selected_account (xferData, other_acct, xferData->quickfill);

    changed = TRUE;
  }

  return( changed );
}

/* The insert_cb will do the insert and quickfill if possible, but won't
 * set the selection or cursor position since the entry widget seems to
 * change these itself.  Instead, a flag will be set and either the
 * key_press_cb or the button_release_cb will set the selection and
 * cursor position stored off from the insert_cb.
 */
static void
gnc_xfer_description_insert_cb(GtkEntry *entry,
                               const gchar *insert_text,
                               const gint insert_text_len,
                               gint *start_pos,
                               XferDialog *xferData)
{
  GString *change_text_gs, *new_text_gs;
  glong old_text_chars, new_text_chars;
  const char *old_text, *match_str = NULL;
  QuickFill *match;
  int i;
  const char *c;
  gunichar uc;

  xferData->desc_didquickfill = FALSE;

  if ( insert_text_len <= 0 )
    return;

  old_text = gtk_entry_get_text (entry);
  if (!old_text)
    old_text = "";

  /* If we are inserting in the middle, do nothing */
  old_text_chars = g_utf8_strlen (old_text, -1);
  if( *start_pos < old_text_chars )
    return;

  change_text_gs = g_string_new_len (insert_text, insert_text_len);

  /* Construct what the new value of the text entry will be */
  new_text_gs = g_string_new ("");
  
  i = 0;
  c = old_text;
  //Copy old text up to insert position
  while ( *c && ( i < *start_pos ) )
  {
    uc = g_utf8_get_char ( c );
    g_string_append_unichar ( new_text_gs, uc );
    c = g_utf8_next_char ( c );
    i++;      
  }

  //Copy inserted text
  g_string_append ( new_text_gs, change_text_gs->str );

  //Copy old text after insert position
  while ( *c )
  {
    uc = g_utf8_get_char ( c );
    g_string_append_unichar ( new_text_gs, uc );
    c = g_utf8_next_char ( c );
  }

  if( ( match = gnc_quickfill_get_string_match( xferData->qf, new_text_gs->str ) )
   && ( match_str = gnc_quickfill_string( match ) ) 
   && safe_strcmp( new_text_gs->str, old_text ) )
  {
    g_signal_handlers_block_matched (G_OBJECT (entry),
				     G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, xferData);

    gtk_entry_set_text( entry, match_str );

    g_signal_handlers_unblock_matched (G_OBJECT (entry),
				       G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, xferData);

    /* stop the current insert */
    g_signal_stop_emission_by_name (G_OBJECT (entry), "insert_text");

    /* This doesn't seem to fix the selection problems, why? */
    gtk_editable_select_region (GTK_EDITABLE(entry), 0, 0);
#if DRH_NEEDS_INVESTIGATION
    gtk_old_editable_claim_selection (GTK_OLD_EDITABLE (entry), FALSE, GDK_CURRENT_TIME);
#endif

    /* Store off data for the key_press_cb or
     * the button_release_cb to make use of. */
    new_text_chars = g_utf8_strlen (new_text_gs->str, -1);
    xferData->desc_cursor_position = new_text_chars;
    xferData->desc_start_selection = new_text_chars;
    xferData->desc_end_selection = -1;
    xferData->desc_didquickfill = TRUE;
  }

  g_string_free (change_text_gs, TRUE);
  g_string_free (new_text_gs, TRUE);
  
}

/* This common post-key press and post-button release handler fixes
 * up the selection and cursor position changes that may be necessary
 * if a quickfill occurred in the insert_cb.
 */
static gboolean
common_post_quickfill_handler(guint32 time, XferDialog *xferData )
{
  GtkEntry *entry = GTK_ENTRY(xferData->description_entry);
  gint current_pos;
  gint current_start;
  gint current_end;
  gboolean did_something = FALSE;   /* was the selection or position changed? */

  ENTER(" ");
  current_pos = gtk_editable_get_position( GTK_EDITABLE(entry) );
  gtk_editable_get_selection_bounds( GTK_EDITABLE(entry),
				     &current_start,
				     &current_end);
  if( current_pos != xferData->desc_cursor_position )
  {
    gtk_entry_set_position( entry, xferData->desc_cursor_position );
    did_something = TRUE;
  }

  if( ( current_start != xferData->desc_start_selection ||
        current_end   != xferData->desc_end_selection      ) &&
      ( xferData->desc_start_selection != xferData->desc_end_selection ||
        xferData->desc_start_selection == 0 ) )
  {
    gtk_entry_select_region( entry, xferData->desc_start_selection,
                                    xferData->desc_end_selection );
#if DRH_NEEDS_INVESTIGATION
    gtk_old_editable_claim_selection( GTK_OLD_EDITABLE(entry), TRUE, time );
#endif
    did_something = TRUE;
  }

  if( did_something ) 
  {
    /* Make sure we don't try to change things again based on these values. */
    xferData->desc_start_selection = current_start;
    xferData->desc_end_selection = current_end;
    xferData->desc_cursor_position = current_pos;
  }

  /* Make sure a new quickfill must occur before coming back through here,
   * whether or not we actually did anything in this function.
   */
  xferData->desc_didquickfill = FALSE;

  LEAVE("did_something=%d", did_something);
  return( did_something );
}

static gboolean
gnc_xfer_description_key_press_cb( GtkEntry *entry,
                                   GdkEventKey *event,
                                   XferDialog *xferData )
{
  gboolean done_with_input = FALSE;

  /* Most "special" keys are allowed to be handled directly by
   * the entry's key press handler, but in some cases that doesn't
   * seem to work right, so handle it here.
   */
  ENTER(" ");
  switch( event->keyval )
  {
    case GDK_Left:        /* right/left cause a focus change which is bad */
    case GDK_KP_Left:
    case GDK_Right:
    case GDK_KP_Right:
      done_with_input = TRUE;
      break;

    case GDK_Return:      /* On the first activate, need to
                           * do the quickfill completion */
    case GDK_KP_Enter:
      if( gnc_xfer_dialog_quickfill( xferData ) )
        done_with_input = TRUE;
      /* Else if no updates were done, allow the keypress to go through,
       * which will result in an activate signal for the dialog.
       */

      break;

    case GDK_Tab:
    case GDK_ISO_Left_Tab:
      if( !( event->state & GDK_SHIFT_MASK) )    /* Complete on Tab,
                                                  * but not Shift-Tab */
      {
        gnc_xfer_dialog_quickfill( xferData );
        /* NOT done with input, though, since we need to focus to the next
         * field.  Unselect the current field, though.
         */
        gtk_entry_select_region( GTK_ENTRY(xferData->description_entry), 0, 0 );
#if DRH_NEEDS_INVESTIGATION
        gtk_old_editable_claim_selection( GTK_OLD_EDITABLE(xferData->description_entry),
                                          FALSE, event->time );
#endif
      }
      break;
  }

  /* Common handling for both key presses and button releases
   * to fix up the selection and cursor position at this point.
   */
  if( !done_with_input && xferData->desc_didquickfill )
    done_with_input = common_post_quickfill_handler( event->time, xferData );

  if( done_with_input )
    g_signal_stop_emission_by_name (G_OBJECT (entry), "key_press_event");

  LEAVE("done=%d", done_with_input);
  return( done_with_input );
}

static gboolean
gnc_xfer_description_button_release_cb( GtkEntry *entry,
                                        GdkEventButton *event,
                                        XferDialog *xferData )
{
  if( xferData->desc_didquickfill )
  {
    /* Common handling for both key presses and button presses
     * to fix up the selection and cursor position at this point.
     */
    common_post_quickfill_handler( event->time, xferData );
  }

  return( FALSE );
}

/*** End of quickfill-specific callbacks ***/

static void
gnc_xfer_dialog_update_conv_info (XferDialog *xferData)
{
  const gchar *to_mnemonic, *from_mnemonic;
  gchar *string;
  gnc_numeric price;

  from_mnemonic = gnc_commodity_get_mnemonic(xferData->from_commodity);
  to_mnemonic = gnc_commodity_get_mnemonic(xferData->to_commodity);

  /* On the theory that if we don't have a mnemonic then we don't
   * have a commodity...  On Solaris this crashes without a string.
   * So, just leave now and wait for the second initialization to
   * occur.
   */
  if (!from_mnemonic || !to_mnemonic)
    return;

  // price = gnc_amount_edit_get_amount(GNC_AMOUNT_EDIT(xferData->price_edit));
  price = gnc_xfer_dialog_compute_price(xferData);

  if (gnc_numeric_check(price) || gnc_numeric_zero_p(price)) {
    string = g_strdup_printf("1 %s = x %s", from_mnemonic, to_mnemonic);
    gtk_label_set_text(GTK_LABEL(xferData->conv_forward), string);
    g_free(string);

    string = g_strdup_printf("1 %s = x %s", to_mnemonic, from_mnemonic);
    gtk_label_set_text(GTK_LABEL(xferData->conv_reverse), string);
    g_free(string);
  } else {
    string = g_strdup_printf("1 %s = %f %s", from_mnemonic,
			     gnc_numeric_to_double(price), to_mnemonic);
    gtk_label_set_text(GTK_LABEL(xferData->conv_forward), string);
    g_free(string);

    price = gnc_numeric_div(gnc_numeric_create (1, 1), price,
			    GNC_DENOM_AUTO, GNC_DENOM_REDUCE);
    string = g_strdup_printf("1 %s = %f %s", to_mnemonic,
			     gnc_numeric_to_double(price), from_mnemonic);
    gtk_label_set_text(GTK_LABEL(xferData->conv_reverse), string);
    g_free(string);
  }
}

static gboolean
gnc_xfer_amount_update_cb(GtkWidget *widget, GdkEventFocus *event,
			  gpointer data)
{
  XferDialog * xferData = data;

  gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT (xferData->amount_edit));

  gnc_xfer_update_to_amount (xferData);

  return FALSE;
}


static void
gnc_xfer_update_to_amount (XferDialog *xferData)
{
  gnc_numeric amount, price, to_amount;
  Account *account;

  account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_TO);
  if (account == NULL)
    account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_FROM);

  if (account == NULL)
  {
    GtkEntry *entry;

    gnc_amount_edit_set_amount(GNC_AMOUNT_EDIT(xferData->to_amount_edit),
                               gnc_numeric_zero ());
    entry = GTK_ENTRY(gnc_amount_edit_gtk_entry
                      (GNC_AMOUNT_EDIT(xferData->to_amount_edit)));
    gtk_entry_set_text(entry, "");
  }

  gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT (xferData->price_edit));

  amount = gnc_amount_edit_get_amount(GNC_AMOUNT_EDIT(xferData->amount_edit));
  price = gnc_amount_edit_get_amount(GNC_AMOUNT_EDIT(xferData->price_edit));

  if (gnc_numeric_zero_p (price))
    to_amount = gnc_numeric_zero ();
  else
    to_amount = gnc_numeric_mul (amount, price,
                                 xaccAccountGetCommoditySCU (account),
                                 GNC_RND_ROUND);

  gnc_amount_edit_set_amount(GNC_AMOUNT_EDIT(xferData->to_amount_edit),
                             to_amount);

  if (gnc_numeric_zero_p (to_amount))
  {
    GtkEntry *entry;

    entry = GTK_ENTRY(gnc_amount_edit_gtk_entry
                      (GNC_AMOUNT_EDIT(xferData->to_amount_edit)));
    gtk_entry_set_text(entry, "");
  }

  gnc_xfer_dialog_update_conv_info(xferData);
}


static gboolean
gnc_xfer_price_update_cb(GtkWidget *widget, GdkEventFocus *event,
			 gpointer data)
{
  XferDialog *xferData = data;

  gnc_xfer_update_to_amount (xferData);

  return FALSE;
}

static gboolean
gnc_xfer_date_changed_cb(GtkWidget *widget, gpointer data)
{
  XferDialog *xferData = data;

  if (xferData)
    gnc_xfer_dialog_update_price (xferData);

  return FALSE;
}

static gboolean
gnc_xfer_to_amount_update_cb(GtkWidget *widget, GdkEventFocus *event,
                             gpointer data)
{
  XferDialog *xferData = data;
  gnc_numeric price;
  Account *account;

  account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_TO);
  if (account == NULL)
    account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_FROM);

  gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT (xferData->to_amount_edit));

  price = gnc_xfer_dialog_compute_price(xferData);
  price = gnc_numeric_convert (price, PRECISION, GNC_RND_ROUND);
  gnc_amount_edit_set_amount(GNC_AMOUNT_EDIT(xferData->price_edit), price);
  gnc_xfer_dialog_update_conv_info(xferData);

  return FALSE;
}


/********************************************************************\
 * gnc_xfer_dialog_select_from_account                              *
 *   select the from account in a xfer dialog                       *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 *         account  - account to select                             *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_select_from_account(XferDialog *xferData, Account *account)
{
  gnc_transfer_dialog_set_selected_account (xferData, account, XFER_DIALOG_FROM);
}


/********************************************************************\
 * gnc_xfer_dialog_select_to_account                                *
 *   select the to account in a xfer dialog                         *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 *         account  - account to select                             *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_select_to_account(XferDialog *xferData, Account *account)
{
  gnc_transfer_dialog_set_selected_account (xferData, account, XFER_DIALOG_TO);
}

void
gnc_xfer_dialog_select_from_currency(XferDialog *xferData, gnc_commodity *cur)
{
  if (!xferData) return;
  if (!cur) return;

  gtk_label_set_text(GTK_LABEL(xferData->from_currency_label), 
		     gnc_commodity_get_printname(cur));

  gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT (xferData->amount_edit),
                                gnc_commodity_get_fraction (cur));

  xferData->from_commodity = cur;
  gnc_xfer_dialog_curr_acct_activate(xferData);
}

void
gnc_xfer_dialog_select_to_currency(XferDialog *xferData, gnc_commodity *cur)
{
  gtk_label_set_text(GTK_LABEL(xferData->to_currency_label),
		     gnc_commodity_get_printname(cur));

  gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT (xferData->to_amount_edit),
				gnc_commodity_get_fraction (cur));

  xferData->to_commodity = cur;
  gnc_xfer_dialog_curr_acct_activate(xferData);
}

static void
gnc_xfer_dialog_lock_account_tree(XferDialog *xferData,
                                  XferDirection direction,
				  gboolean hide)
{
  GtkTreeView *tree_view;
  GtkWidget *show_button;
  GtkWidget *scroll_win;

  if (xferData == NULL)
    return;

  switch (direction)
  {
    case XFER_DIALOG_FROM:
      tree_view = xferData->from_tree_view;
      scroll_win = xferData->from_window;
      show_button = xferData->from_show_button;
      break;
    case XFER_DIALOG_TO:
      tree_view = xferData->to_tree_view;
      scroll_win = xferData->to_window;
      show_button = xferData->to_show_button;
      break;
    default:
      return;
  }

  gtk_widget_set_sensitive( GTK_WIDGET(tree_view), FALSE );
  gtk_widget_set_sensitive( GTK_WIDGET(show_button), FALSE );

  if (hide) {
    gtk_widget_hide( scroll_win );
    gtk_widget_hide( GTK_WIDGET(show_button) );
  }
}


/********************************************************************\
 * gnc_xfer_dialog_lock_from_account_tree                           *
 *   prevent changes to the from account tree in an xfer dialog     *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_lock_from_account_tree(XferDialog *xferData)
{
  gnc_xfer_dialog_lock_account_tree(xferData, XFER_DIALOG_FROM, FALSE);
}


/********************************************************************\
 * gnc_xfer_dialog_lock_to_account_tree                             *
 *   prevent changes to the to account tree in an xfer dialog       *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_lock_to_account_tree(XferDialog *xferData)
{
  gnc_xfer_dialog_lock_account_tree(xferData, XFER_DIALOG_TO, FALSE);
}


/********************************************************************\
 * gnc_xfer_dialog_hide_from_account_tree                           *
 *   prevent changes to the from account tree in an xfer dialog     *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_hide_from_account_tree(XferDialog *xferData)
{
  gnc_xfer_dialog_lock_account_tree(xferData, XFER_DIALOG_FROM, TRUE);
}


/********************************************************************\
 * gnc_xfer_dialog_hide_to_account_tree                             *
 *   prevent changes to the to account tree in an xfer dialog       *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_hide_to_account_tree(XferDialog *xferData)
{
  gnc_xfer_dialog_lock_account_tree(xferData, XFER_DIALOG_TO, TRUE);
}


/********************************************************************\
 * gnc_xfer_dialog_is_exchange_dialog                               *
 *   set the dialog as an "exchange-dialog", which means that the   *
 *   Transfer Information table is read-only (and the dialog        *
 *   will NOT create a transaction when it is closed)               *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 *         exch_rate - place to store the exchange rate at exit     *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_is_exchange_dialog (XferDialog *xferData, gnc_numeric *exch_rate)
{
  GNCAmountEdit *gae;

  if (!xferData) return;

  gtk_widget_set_sensitive (xferData->amount_edit, FALSE);
  gtk_widget_set_sensitive (xferData->date_entry, FALSE);
  gtk_widget_set_sensitive (xferData->num_entry, FALSE);
  gtk_widget_set_sensitive (xferData->description_entry, FALSE);
  gtk_widget_set_sensitive (xferData->memo_entry, FALSE);


  gae = GNC_AMOUNT_EDIT (xferData->price_edit);
  gtk_widget_grab_focus (gnc_amount_edit_gtk_entry (gae));

  xferData->exch_rate = exch_rate;
}

/********************************************************************\
 * gnc_xfer_dialog_set_amount                                       *
 *   set the amount in the given xfer dialog                        *
 *                                                                  *
 * Args:   xferData - xfer dialog structure                         *
 *         amount   - the amount to set                             *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_set_amount(XferDialog *xferData, gnc_numeric amount)
{
  Account * account;

  if (xferData == NULL)
    return;

  account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_FROM);
  if (account == NULL)
    account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_TO);

  gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT (xferData->amount_edit), amount);
}


/********************************************************************\
 * gnc_xfer_dialog_set_description                                  *
 *   set the description in the given xfer dialog                   *
 *                                                                  *
 * Args:   xferData    - xfer dialog structure                      *
 *         description - the description to set                     *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_set_description(XferDialog *xferData, const char *description)
{
  if (xferData == NULL)
    return;

  gtk_entry_set_text(GTK_ENTRY(xferData->description_entry), description);
  gnc_quickfill_insert( xferData->qf, description, QUICKFILL_LIFO );
}

/********************************************************************\
 * gnc_xfer_dialog_set_memo                                         *
 *   set the memo in the given xfer dialog                          *
 *                                                                  *
 * Args:   xferData    - xfer dialog structure                      *
 *         memo        - the memo to set                            *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_set_memo(XferDialog *xferData, const char *memo)
{
  if (xferData == NULL)
    return;

  gtk_entry_set_text(GTK_ENTRY(xferData->memo_entry), memo);
  /* gnc_quickfill_insert( xferData->qf, memo, QUICKFILL_LIFO ); */
}

/********************************************************************\
 * gnc_xfer_dialog_set_num                                          *
 *   set the num in the given xfer dialog                           *
 *                                                                  *
 * Args:   xferData    - xfer dialog structure                      *
 *         num        - the num to set                              *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_set_num(XferDialog *xferData, const char *num)
{
  if (xferData == NULL)
    return;

  gtk_entry_set_text(GTK_ENTRY(xferData->num_entry), num);
  /* gnc_quickfill_insert( xferData->qf, num, QUICKFILL_LIFO ); */
}

/********************************************************************\
 * gnc_xfer_dialog_set_date                                         *
 *   set the date in the given xfer dialog                          *
 *                                                                  *
 * Args:   xferData    - xfer dialog structure                      *
 *         set_date    - the date to set                            *
 * Return: none                                                     *
\********************************************************************/
void
gnc_xfer_dialog_set_date(XferDialog *xferData, time_t set_date)
{
   if (xferData == NULL)
      return;

   gnc_date_edit_set_time( GNC_DATE_EDIT(xferData->date_entry), set_date );
}

void
gnc_xfer_dialog_set_exchange_rate(XferDialog *xferData, gnc_numeric exchange_rate)
{
  if (xferData == NULL)
    return;

  if (gnc_numeric_zero_p (exchange_rate))
    return;

  gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT (xferData->price_edit),
			      exchange_rate);
  
  gnc_xfer_update_to_amount (xferData);
}

void
gnc_xfer_dialog_response_cb (GtkDialog *dialog, gint response, gpointer data)
{
  XferDialog *xferData = data;
  Account *to_account;
  Account *from_account;
  gnc_commodity *from_commodity;
  gnc_commodity *to_commodity;
  gnc_numeric amount, to_amount;
  const char *string;
  Timespec ts;

  gboolean curr_trans;

  Transaction *trans;
  Split *from_split;
  Split *to_split;

  ENTER(" ");
  if (response != GTK_RESPONSE_OK) {
    gnc_close_gui_component_by_data (DIALOG_TRANSFER_CM_CLASS, xferData);
    LEAVE("cancel, etc.");
    return;
  }

  from_account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_FROM);
  to_account = gnc_transfer_dialog_get_selected_account (xferData, XFER_DIALOG_TO);

  if (xferData->exch_rate == NULL)
  {
    if ((from_account == NULL) || (to_account == NULL))
    {
      const char *message = _("You must specify an account to transfer from,\n"
			      "or to, or both, for this transaction.\n"
			      "Otherwise, it will not be recorded.");
      gnc_error_dialog(xferData->dialog, message);
      LEAVE("bad account");
      return;
    }

    if (from_account == to_account)
    {
      const char *message = _("You can't transfer from and to the same "
			      "account!");
      gnc_error_dialog(xferData->dialog, message);
      LEAVE("same account");
      return;
    }

    if (xaccAccountGetPlaceholder(from_account) ||
	xaccAccountGetPlaceholder(to_account))
    {
      const char *placeholder_format =
	_("The account %s\ndoes not allow transactions.\n");
      char *name;

      if (xaccAccountGetPlaceholder(from_account))
	name = xaccAccountGetFullName(from_account,
				      gnc_get_account_separator ());
      else
	name = xaccAccountGetFullName(to_account,
				      gnc_get_account_separator ());
      gnc_error_dialog(xferData->dialog, placeholder_format, name);
      g_free(name);
      LEAVE("placeholder");
      return;
    }

    if (!gnc_commodity_is_iso (xferData->from_commodity))
    {
      const char *message = _("You can't transfer from a non-currency account.  "
			      "Try reversing the \"from\" and \"to\" accounts "
			      "and making the \"amount\" negative.");
      gnc_error_dialog(xferData->dialog, message);
      LEAVE("non-currency");
      return;
    }
  }

  if (!gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT (xferData->amount_edit)))
  {
    gnc_parse_error_dialog (xferData, _("You must enter a valid amount."));
    LEAVE("no account");
    return;
  }

  from_commodity = xferData->from_commodity;
  to_commodity = xferData->to_commodity;

  curr_trans = !gnc_commodity_equiv(from_commodity, to_commodity);

  amount = gnc_amount_edit_get_amount(GNC_AMOUNT_EDIT(xferData->amount_edit));

  if (gnc_numeric_zero_p (amount))
  {
    const char *message = _("You must enter an amount to transfer.");
    gnc_error_dialog(xferData->dialog, message);
    LEAVE("invalid from amount");
    return;
  }

  ts = gnc_date_edit_get_date_ts(GNC_DATE_EDIT(xferData->date_entry));

  if (curr_trans)
  {
    if (!gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT (xferData->price_edit)))
    {
      if (gtk_toggle_button_get_active
          (GTK_TOGGLE_BUTTON(xferData->price_radio)))
      {
	gnc_parse_error_dialog (xferData, _("You must enter a valid price."));
	LEAVE("invalid price");
	return;
      }
    }

    if (!gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT (xferData->to_amount_edit)))
    {
      if (gtk_toggle_button_get_active
          (GTK_TOGGLE_BUTTON(xferData->amount_radio)))
      {
	gnc_parse_error_dialog (xferData,
                                _("You must enter a valid `to' amount."));
	LEAVE("invalid to amount");
	return;
      }
    }

    to_amount = gnc_amount_edit_get_amount
      (GNC_AMOUNT_EDIT(xferData->to_amount_edit));
  }
  else
    to_amount = amount;

  gnc_suspend_gui_refresh ();

  if (xferData->exch_rate)
  {
    gnc_numeric price;

    /* If we've got the price-button set, then make sure we update the
     * to-amount before we use it.
     */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(xferData->price_radio)))
      gnc_xfer_update_to_amount(xferData);

    price = gnc_xfer_dialog_compute_price(xferData);
    *(xferData->exch_rate) = gnc_numeric_abs(price);
  }
  else
  {
    /* Create the transaction */
    trans = xaccMallocTransaction(xferData->book);

    xaccTransBeginEdit(trans);

    xaccTransSetCurrency(trans, from_commodity);
    xaccTransSetDatePostedTS(trans, &ts);

    string = gtk_entry_get_text(GTK_ENTRY(xferData->num_entry));
    xaccTransSetNum(trans, string);

    string = gtk_entry_get_text(GTK_ENTRY(xferData->description_entry));
    xaccTransSetDescription(trans, string);

    /* create from split */
    from_split = xaccMallocSplit(xferData->book);
    xaccTransAppendSplit(trans, from_split); 

    /* create to split */
    to_split = xaccMallocSplit(xferData->book);
    xaccTransAppendSplit(trans, to_split); 

    xaccAccountBeginEdit(from_account);
    xaccAccountInsertSplit(from_account, from_split);

    xaccAccountBeginEdit(to_account);
    xaccAccountInsertSplit(to_account, to_split);

    xaccSplitSetBaseValue(from_split, gnc_numeric_neg (amount), from_commodity);
    xaccSplitSetBaseValue(to_split, amount, from_commodity);
    xaccSplitSetBaseValue(to_split, to_amount, to_commodity);

    /* Set the memo fields */
    string = gtk_entry_get_text(GTK_ENTRY(xferData->memo_entry));
    xaccSplitSetMemo(from_split, string);
    xaccSplitSetMemo(to_split, string);

    /* finish transaction */
    xaccTransCommitEdit(trans);
    xaccAccountCommitEdit(from_account);
    xaccAccountCommitEdit(to_account);

    /* If there is a registered callback handler that should be
       notified of the newly created Transaction, call it now. */
    if (xferData->transaction_cb)
      xferData->transaction_cb(trans, xferData->transaction_user_data);
  }

  /* try to save this to the pricedb */
  if (xferData->pricedb) {
    gnc_commodity *from = xferData->from_commodity;
    gnc_commodity *to = xferData->to_commodity;

    /* only continue if the currencies are DIFFERENT and are
     * not both euroland currencies 
     */
    if (!gnc_commodity_equal (from, to) &&
	!(gnc_is_euro_currency (from) && gnc_is_euro_currency (to)))
    {
      GNCPrice *price;
      GList *prices;

      /* First see if an entry exists at time ts */
      prices = gnc_pricedb_lookup_at_time (xferData->pricedb, from, to, ts);
      if (prices) {
	PINFO("Found price for %s in %s", gnc_commodity_get_mnemonic(from),
	      gnc_commodity_get_mnemonic(to));
      } else {
	prices = gnc_pricedb_lookup_at_time (xferData->pricedb, to, from, ts);
	if (prices) {
	  PINFO("Found reverse price for %s in %s", gnc_commodity_get_mnemonic(to),
		gnc_commodity_get_mnemonic(from));
	}
      }

      /* If so, do nothing (well, destroy the list).  if not, create one. */
      if (prices) {
	gnc_price_list_destroy (prices);
      } else {
	gnc_commodity *tmp;
	gnc_numeric value;

	/* compute the price -- maybe we need to swap? */
	value = gnc_xfer_dialog_compute_price(xferData);
	value = gnc_numeric_abs (value);

	/* Try to be consistent about how quotes are installed. */
	if (from == gnc_default_currency()) {
	  tmp = from; from = to; to = tmp;
	  value = gnc_numeric_div (gnc_numeric_create(1,1), value,
				   GNC_DENOM_AUTO, GNC_DENOM_REDUCE);
	} else if ((to != gnc_default_currency()) &&
		   (strcmp (gnc_commodity_get_mnemonic(from),
			    gnc_commodity_get_mnemonic(to)) < 0)) {
	  tmp = from; from = to; to = tmp;
	  value = gnc_numeric_div (gnc_numeric_create(1,1), value,
				   GNC_DENOM_AUTO, GNC_DENOM_REDUCE);
	}

	price = gnc_price_create (xferData->book);
	gnc_price_begin_edit (price);
	gnc_price_set_commodity (price, from);
	gnc_price_set_currency (price, to);
	gnc_price_set_time (price, ts);
	gnc_price_set_source (price, "user:xfer-dialog");
	gnc_price_set_value (price, value);
	gnc_pricedb_add_price (xferData->pricedb, price);
	gnc_price_commit_edit (price);
	gnc_price_unref (price);
	PINFO("Created price: 1 %s = %f %s", gnc_commodity_get_mnemonic(from),
	      gnc_numeric_to_double(value), gnc_commodity_get_mnemonic(to));
      }
    }
  }

  /* Refresh everything */
  gnc_resume_gui_refresh ();

  DEBUG("close component");
  gnc_close_gui_component_by_data (DIALOG_TRANSFER_CM_CLASS, xferData);
  LEAVE("ok");
}

void
gnc_xfer_dialog_close_cb(GtkDialog *dialog, gpointer data)
{
  XferDialog * xferData = data;
  GtkWidget *entry;

  /* Notify transaction callback to unregister here */
  if (xferData->transaction_cb)
    xferData->transaction_cb(NULL, xferData->transaction_user_data);

  entry = gnc_amount_edit_gtk_entry(GNC_AMOUNT_EDIT(xferData->amount_edit));
  g_signal_handlers_disconnect_matched (G_OBJECT (entry), G_SIGNAL_MATCH_DATA,
					0, 0, NULL, NULL, xferData);

  entry = gnc_amount_edit_gtk_entry(GNC_AMOUNT_EDIT(xferData->price_edit));
  g_signal_handlers_disconnect_matched (G_OBJECT (entry), G_SIGNAL_MATCH_DATA,
					0, 0, NULL, NULL, xferData);

  entry = gnc_amount_edit_gtk_entry(GNC_AMOUNT_EDIT(xferData->to_amount_edit));
  g_signal_handlers_disconnect_matched (G_OBJECT (entry), G_SIGNAL_MATCH_DATA,
					0, 0, NULL, NULL, xferData);

  entry = xferData->description_entry;
  g_signal_handlers_disconnect_matched (G_OBJECT (entry), G_SIGNAL_MATCH_DATA,
					0, 0, NULL, NULL, xferData);

  g_object_unref (xferData->tips);

  DEBUG("unregister component");
  gnc_unregister_gui_component_by_data (DIALOG_TRANSFER_CM_CLASS, xferData);

  gnc_quickfill_destroy (xferData->qf);
  xferData->qf = NULL;

  g_free(xferData);

  DEBUG("xfer dialog destroyed");
}


static void
gnc_xfer_dialog_create(GtkWidget *parent, XferDialog *xferData)
{
  GtkWidget *dialog;
  GladeXML  *xml;
  gboolean  use_accounting_labels;

  use_accounting_labels = gnc_gconf_get_bool(GCONF_GENERAL,
					     KEY_ACCOUNTING_LABELS, NULL);

  ENTER(" ");
  xml = gnc_glade_xml_new ("transfer.glade", "Transfer Dialog");

  dialog = glade_xml_get_widget (xml, "Transfer Dialog");
  xferData->dialog = dialog;

  /* parent */
  if (parent != NULL)
    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));

  glade_xml_signal_autoconnect_full(xml, gnc_glade_autoconnect_full_func, xferData);

  xferData->tips = gtk_tooltips_new();

  g_object_ref (xferData->tips);
  gtk_object_sink (GTK_OBJECT (xferData->tips));

  /* default to quickfilling off of the "From" account. */
  xferData->quickfill = XFER_DIALOG_FROM;

  xferData->transferinfo_label = glade_xml_get_widget (xml, "transferinfo-label");

  /* amount & date widgets */
  {
    GtkWidget *amount;
    GtkWidget *entry;
    GtkWidget *date;
    GtkWidget *hbox;

    amount = gnc_amount_edit_new();
    hbox = glade_xml_get_widget (xml, "amount_hbox");
    gtk_box_pack_end(GTK_BOX(hbox), amount, TRUE, TRUE, 0);
    gnc_amount_edit_set_evaluate_on_enter (GNC_AMOUNT_EDIT (amount), TRUE);
    xferData->amount_edit = amount;

    entry = gnc_amount_edit_gtk_entry (GNC_AMOUNT_EDIT (amount));
    g_signal_connect (G_OBJECT (entry), "focus-out-event",
		      G_CALLBACK (gnc_xfer_amount_update_cb), xferData);

    date = gnc_date_edit_new(time (NULL), FALSE, FALSE);
    hbox = glade_xml_get_widget (xml, "date_hbox");

    gtk_box_pack_end(GTK_BOX(hbox), date, TRUE, TRUE, 0);
    xferData->date_entry = date;
    g_signal_connect (G_OBJECT (date), "date_changed",
		      G_CALLBACK (gnc_xfer_date_changed_cb), xferData);
  }

  {
    GtkWidget *entry;

    entry = glade_xml_get_widget (xml, "num_entry");
    xferData->num_entry = entry;

    entry = glade_xml_get_widget (xml, "description_entry");
    xferData->description_entry = entry;

    /* Get signals from the Description for quickfill. */
    g_signal_connect (G_OBJECT (entry), "insert_text",
		      G_CALLBACK (gnc_xfer_description_insert_cb), xferData);
    g_signal_connect (G_OBJECT (entry), "button_release_event",
		      G_CALLBACK (gnc_xfer_description_button_release_cb), xferData);
    g_signal_connect_after (G_OBJECT (entry), "key_press_event",
			    G_CALLBACK (gnc_xfer_description_key_press_cb), xferData);

    entry = glade_xml_get_widget (xml, "memo_entry");
    xferData->memo_entry = entry;
  }

  /* from and to */
  {
    GtkWidget *label;
    gchar *text;

    gnc_xfer_dialog_fill_tree_view (xferData, XFER_DIALOG_TO);
    gnc_xfer_dialog_fill_tree_view (xferData, XFER_DIALOG_FROM);

    /* Reverse from and to account trees when in "accountant" mode,
       see comment in function gnc_xfer_dialog_fill_tree_table */
    if(use_accounting_labels) {
      label = glade_xml_get_widget (xml, "right_trans_label");
      xferData->from_transfer_label = label;

      label = glade_xml_get_widget (xml, "left_trans_label");
      xferData->to_transfer_label = label;

      text = g_strconcat ("<b>", _("Credit Account"), "</b>", NULL);
      gtk_label_set_markup (GTK_LABEL (xferData->from_transfer_label), text);
      g_free (text);

      text = g_strconcat ("<b>", _("Debit Account"), "</b>", NULL);
      gtk_label_set_markup (GTK_LABEL (xferData->to_transfer_label), text);
      g_free (text);

      label = glade_xml_get_widget (xml, "right_currency_label");
      xferData->from_currency_label = label;

      label = glade_xml_get_widget (xml, "left_currency_label");
      xferData->to_currency_label = label;
    }
    else {
      label = glade_xml_get_widget (xml, "left_trans_label");
      xferData->from_transfer_label = label;

      label = glade_xml_get_widget (xml, "right_trans_label");
      xferData->to_transfer_label = label;

      text = g_strconcat ("<b>", _("Transfer From"), "</b>", NULL);
      gtk_label_set_markup (GTK_LABEL (xferData->from_transfer_label), text);
      g_free (text);

      text = g_strconcat ("<b>", _("Transfer To"), "</b>", NULL);
      gtk_label_set_markup (GTK_LABEL (xferData->to_transfer_label), text);

      label = glade_xml_get_widget (xml, "left_currency_label");
      xferData->from_currency_label = label;

      label = glade_xml_get_widget (xml, "right_currency_label");
      xferData->to_currency_label = label;
    }

    label = glade_xml_get_widget (xml, "conv_forward");
    xferData->conv_forward = label;

    label = glade_xml_get_widget (xml, "conv_reverse");
    xferData->conv_reverse = label;
  }

  /* optional intermediate currency account */
  {
    GtkWidget *table;
    GtkWidget *entry;
    GtkWidget *edit;
    GtkWidget *hbox;
    GtkWidget *button;

    table = glade_xml_get_widget (xml, "curr_transfer_table");
    xferData->curr_xfer_table = table;

    edit = gnc_amount_edit_new();
    gnc_amount_edit_set_print_info(GNC_AMOUNT_EDIT(edit),
                                   gnc_default_print_info (FALSE));
    gnc_amount_edit_set_fraction(GNC_AMOUNT_EDIT(edit), PRECISION);
    hbox = glade_xml_get_widget (xml, "price_hbox");
    gtk_box_pack_start(GTK_BOX(hbox), edit, TRUE, TRUE, 0);
    xferData->price_edit = edit;
    entry = gnc_amount_edit_gtk_entry (GNC_AMOUNT_EDIT (edit));
    g_signal_connect (G_OBJECT (entry), "focus-out-event",
		      G_CALLBACK (gnc_xfer_price_update_cb), xferData);
    gtk_entry_set_activates_default(GTK_ENTRY (entry), TRUE);

    edit = gnc_amount_edit_new();
    hbox = glade_xml_get_widget (xml, "right_amount_hbox");
    gtk_box_pack_start(GTK_BOX(hbox), edit, TRUE, TRUE, 0);
    xferData->to_amount_edit = edit;
    entry = gnc_amount_edit_gtk_entry (GNC_AMOUNT_EDIT (edit));
    g_signal_connect (G_OBJECT (entry), "focus-out-event",
		      G_CALLBACK (gnc_xfer_to_amount_update_cb), xferData);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

    button = glade_xml_get_widget (xml, "price_radio");
    xferData->price_radio = button;
    g_signal_connect (G_OBJECT (xferData->price_radio), "toggled",
		      G_CALLBACK (price_amount_radio_toggled_cb), xferData);

    button = glade_xml_get_widget (xml, "amount_radio");
    xferData->amount_radio = button;
    g_signal_connect(G_OBJECT(xferData->amount_radio), "toggled",
		     G_CALLBACK(price_amount_radio_toggled_cb),
		     xferData);
    if(use_accounting_labels) {
      gtk_label_set_text(GTK_LABEL(GTK_BIN(xferData->amount_radio)->child),
			 _("Debit Amount:"));
    }
    else {
      gtk_label_set_text(GTK_LABEL(GTK_BIN(xferData->amount_radio)->child),
			 _("To Amount:"));
    }
  }
  LEAVE(" ");
}

static void
close_handler (gpointer user_data)
{
  XferDialog *xferData = user_data;
  GtkWidget *dialog;

  ENTER(" ");
  dialog = GTK_WIDGET (xferData->dialog);

  gtk_widget_hide (dialog);
  gnc_xfer_dialog_close_cb(GTK_DIALOG(dialog), xferData);
  gtk_widget_destroy (dialog);
  LEAVE(" ");
}

/********************************************************************\
 * gnc_xfer_dialog                                                  *
 *   opens up a window to do an automatic transfer between accounts *
 *                                                                  * 
 * Args:   parent  - the parent of the window to be created         *
 *         initial - the initial account in the from/to fields      *
 * Return: XferDialog structure                                     *
\********************************************************************/
XferDialog *
gnc_xfer_dialog (GtkWidget * parent, Account * initial)
{
  XferDialog *xferData;
  GNCAmountEdit *gae;
  GtkWidget *amount_entry;
  GNCBook *book = NULL;

  xferData = g_new0 (XferDialog, 1);

  xferData->desc_cursor_position = 0;
  xferData->desc_start_selection = 0;
  xferData->desc_end_selection = 0;
  xferData->desc_didquickfill = FALSE;
  xferData->quickfill = XFER_DIALOG_FROM;
  xferData->transaction_cb = NULL;

  if (initial) {
    book = xaccAccountGetBook (initial);
  } else {
    book = gnc_get_current_book ();
  }

  xferData->book = book;
  xferData->pricedb = gnc_book_get_pricedb (book);

  gnc_xfer_dialog_create(parent, xferData);

  DEBUG("register component");
  gnc_register_gui_component (DIALOG_TRANSFER_CM_CLASS,
                              NULL, close_handler, xferData);

  gae = GNC_AMOUNT_EDIT(xferData->amount_edit);
  amount_entry = gnc_amount_edit_gtk_entry (gae);

  gtk_widget_grab_focus(amount_entry);

  gnc_xfer_dialog_select_from_account(xferData, initial);
  gnc_xfer_dialog_select_to_account(xferData, initial);

  gnc_xfer_dialog_curr_acct_activate(xferData);

  gtk_widget_show_all(xferData->dialog);

  gnc_window_adjust_for_screen(GTK_WINDOW(xferData->dialog));

  return xferData;
}

void
gnc_xfer_dialog_close( XferDialog *xferData )
{
  if( xferData ) {
    DEBUG("close component");
    gtk_dialog_response( GTK_DIALOG(xferData->dialog), GTK_RESPONSE_NONE );
  }
}

void
gnc_xfer_dialog_set_title( XferDialog *xferData, const gchar *title )
{
  if( xferData && title )
  {
    gtk_window_set_title (GTK_WINDOW (xferData->dialog), title);
  }
}

void
gnc_xfer_dialog_set_information_label( XferDialog *xferData,
				       const gchar *text )
{
  if(xferData && text)
    gtk_label_set_label (GTK_LABEL (xferData->transferinfo_label), text);
}


static void
gnc_xfer_dialog_set_account_label( XferDialog *xferData,
				   const gchar *text,
				   XferDirection direction )
{
  if(xferData && text)
    gtk_label_set_text (GTK_LABEL ((direction == XFER_DIALOG_FROM ?
				    xferData->from_transfer_label :
				    xferData->to_transfer_label)),
		    	text);
}

void
gnc_xfer_dialog_set_from_account_label( XferDialog *xferData,
					const gchar *label )
{
  gnc_xfer_dialog_set_account_label (xferData, label, XFER_DIALOG_FROM);
}

void
gnc_xfer_dialog_set_to_account_label( XferDialog *xferData,
				      const gchar *label )
{
  gnc_xfer_dialog_set_account_label (xferData, label, XFER_DIALOG_TO);
}

void
gnc_xfer_dialog_set_from_show_button_active( XferDialog *xferData,
                                             gboolean set_value )
{
  if( xferData && xferData->from_show_button )
  {
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(xferData->from_show_button),
                                                    set_value );
  }
}

void
gnc_xfer_dialog_set_to_show_button_active( XferDialog *xferData,
                                           gboolean set_value )
{
  if( xferData && xferData->to_show_button )
  {
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(xferData->to_show_button),
                                  set_value );
  }
}

/* Add a button with a user-specified label and "clicked" callback */
void gnc_xfer_dialog_add_user_specified_button( XferDialog *xferData,
                                                const gchar *label,
                                                GtkSignalFunc callback,
                                                gpointer user_data )
{
  if( xferData && label && callback )
  {
    GtkWidget *button = gtk_button_new_with_label( label );
    GtkWidget *box    = gnc_glade_lookup_widget (xferData->dialog,
                                                 "transfermain-vbox" );
    gtk_box_pack_end( GTK_BOX(box), button, FALSE, FALSE, 0 );
    g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (callback), user_data);
    gtk_widget_show( button );
  }
}

void gnc_xfer_dialog_toggle_currency_table( XferDialog *xferData, 
                                            gboolean show_table )
{
  if (xferData && xferData->curr_xfer_table)
  {
    if (show_table)
      gtk_widget_show(xferData->curr_xfer_table);
    else
      gtk_widget_hide(xferData->curr_xfer_table);
  }
}


/* helper function */
static gboolean
find_xfer (gpointer find_data, gpointer user_data)
{
  return( find_data == user_data );
}

/* Run the dialog until the user has either successfully completed the
 * transaction (just clicking OK doesn't always count) or clicked Cancel.
 * Return TRUE if the transaction was a success, FALSE otherwise.
 */
gboolean gnc_xfer_dialog_run_until_done( XferDialog *xferData )
{
  GtkDialog *dialog;
  gint count, response;

  ENTER("xferData=%p", xferData);
  if( xferData == NULL ) {
    LEAVE("bad args");
    return( FALSE );
  }

  dialog = GTK_DIALOG (xferData->dialog);

  /*
   * We need to call the response_cb function by hand.  Calling it
   * automatically on a button click can destroy the window, and
   * that's bad mojo whole gtk_dialog_run is still in control.
   */
  count = g_signal_handlers_disconnect_by_func(dialog,
					       gnc_xfer_dialog_response_cb,
					       xferData);
  g_assert(count == 1);

  while( TRUE ) {
    DEBUG("calling gtk_dialog_run");
    response = gtk_dialog_run (dialog);
    DEBUG("gtk_dialog_run returned %d", response);
    gnc_xfer_dialog_response_cb (dialog, response, xferData);

    if (response != GTK_RESPONSE_OK) {
      LEAVE("not ok");
      return FALSE;
    }

    /* See if the dialog is still there.  For various reasons, the
     * user could have hit OK but remained in the dialog.  We don't
     * want to return processing back to anyone else until we clear
     * off this dialog, so if the dialog is still there we'll just
     * run it again.
     */
    if( !gnc_find_first_gui_component( DIALOG_TRANSFER_CM_CLASS,
				       find_xfer, xferData ) )
      {
	/* no more dialog, and OK was clicked, so assume it's all good */
	LEAVE("ok");
	return TRUE;
      }
    
    /* else run the dialog again */
  }

  g_assert_not_reached();
}


/* Indicate that the dialog should quickfill based on the "To" account,
 * rather than the default which is the "From" account.
 */

void
gnc_xfer_dialog_quickfill_to_account(XferDialog *xferData,
                                     gboolean qf_to_account )
{
  XferDirection old = xferData->quickfill;

  xferData->quickfill = qf_to_account ? XFER_DIALOG_TO : XFER_DIALOG_FROM;

  /* reload the quickfill if necessary */
  if( old != xferData->quickfill )
    gnc_xfer_dialog_reload_quickfill( xferData );
}

static Account *
gnc_transfer_dialog_get_selected_account (XferDialog *dialog,
					  XferDirection direction)
{
  GtkTreeView *tree_view;
  Account *account;

  switch (direction) {
   case XFER_DIALOG_FROM:
    tree_view = dialog->from_tree_view;
    break;
   case XFER_DIALOG_TO:
    tree_view = dialog->to_tree_view;
    break;
   default:
    g_assert_not_reached ();
    return NULL;
  }

  account = gnc_tree_view_account_get_selected_account  (GNC_TREE_VIEW_ACCOUNT (tree_view));
  return account;
}

static void
gnc_transfer_dialog_set_selected_account (XferDialog *dialog,
					  Account *account,
					  XferDirection direction)
{
  GtkTreeView *tree_view;
  GtkCheckButton *show_button;
  GNCAccountType type;

  if (account == NULL)
    return;

  switch (direction) {
   case XFER_DIALOG_FROM:
    tree_view = dialog->from_tree_view;
    show_button = GTK_CHECK_BUTTON (dialog->from_show_button);
    break;
   case XFER_DIALOG_TO:
    tree_view = dialog->to_tree_view;
    show_button = GTK_CHECK_BUTTON (dialog->to_show_button);
    break;
   default:
    g_assert_not_reached ();
    return;
  }

  type = xaccAccountGetType (account);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_button),
				(type == EXPENSE) || (type == INCOME));

  gnc_tree_view_account_set_selected_account (GNC_TREE_VIEW_ACCOUNT (tree_view),
					      account);
}


void gnc_xfer_dialog_set_txn_cb(XferDialog *xferData,
				gnc_xfer_dialog_cb handler, 
				gpointer user_data)
{
  g_assert(xferData);
  xferData->transaction_cb = handler;
  xferData->transaction_user_data = user_data;
}