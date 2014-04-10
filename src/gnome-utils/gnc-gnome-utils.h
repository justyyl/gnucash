/********************************************************************\
 * gnc-gnome-utils.h -- utility functions for gnome for GnuCash     *
 * Copyright (C) 2001 Linux Developers Group                        *
 * Copyright (C) 2003 David Hampton <hampton@employees.org>         *
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
 *                                                                  *
\********************************************************************/

/** @addtogroup GUI
    @{ */
/** @addtogroup GuiGnome Gnome-specific GUI handling.
    @{ */
/** @file gnc-gnome-utils.h
    @brief Gnome specific utility functions.
    @author Copyright (C) 2001 Linux Developers Group
    @author Copyright (C) 2003 David Hampton <hampton@employees.org>    
*/

#ifndef GNC_GNOME_UTILS_H
#define GNC_GNOME_UTILS_H

#ifdef LIBGUILEH
/** Initialize the Gnome libraries.
 *
 *  @param arg0 The running application as it appears to a user.
 *
 *  @param program The compiled name of the application. "gnucash"
 *
 *  @param version The program version. (e.g. 1.8.7)
 *
 *  @param command_line A scheme list containing all of the command
 *  line arguments (or all of the arguments notyet pasrsed in scheme).
 *
 *  @return The initial command_line argument minus any arguments
 *  parsed by this function.
 */
SCM gnc_gnome_init (const char * arg0,
                    const char * progname,
                    const char * version,
                    SCM command_line);
#endif

/** Shutdown/cleanup any gnome related libraries. */
void gnc_gnome_shutdown (void);


/** Given a pixmap/pixbuf file name, find the file in the pixmap
 *  directory associated with this application.  This routine will
 *  display an error message if it can't find the file.
 *
 *  @param name The name of the file to be found.
 *
 *  @return the full path name of the file, or NULL of the file can't
 *  be found.
 *
 *  @note It is the caller's responsibility to free the returned string.
 */
char *gnc_gnome_locate_pixmap (const char *name);


/** Given a file name, find the file in the directories associated
 *  with this application.  This routine will display an error message
 *  if it can't find the file.
 *
 *  @param name The name of the file to be found.
 *
 *  @return the full path name of the file, or NULL of the file can't
 *  be found.
 *
 *  @note It is the caller's responsibility to free the returned string.
 */
char *gnc_gnome_locate_data_file (const char *name);


/** Given a file name, find the file in the directories associated
 *  with this application.  This routine will display an error message
 *  if it can't find the file.
 *
 *  @param name The name of the file to be found.
 *
 *  @return the full path name of the file, or NULL of the file can't
 *  be found.
 *
 *  @note It is the caller's responsibility to free the returned string.
 */
char *gnc_gnome_locate_ui_file (const char *name);


/** Launch the default gnome help browser and open to a given link
 *  within a given file.  This routine will display an error message
 *  if it can't find the help file or can't open the help browser.
 *
 *  @param file_name The name of the help file.
 *
 *  @param anchor The anchor the help browser should scroll to..
 *
 *  @return the full path name of the file, or NULL of the file can't
 *  be found.
 */
void gnc_gnome_help (const char *file_name,
		     const char *anchor);


/** Given a file name, find and load the requested pixmap.  This
 *  routine will display an error message if it can't find the file or
 *  load the pixmap.
 *
 *  @param name The name of the pixmap file to load.
 *
 *  @return A pointer to the pixmap, or NULL of the file couldn't
 *  be found or loaded..
 */
GtkWidget * gnc_gnome_get_pixmap (const char *name);


/** Given a file name, find and load the requested pixbuf.  This
 *  routine will display an error message if it can't find the file or
 *  load the pixbuf.
 *
 *  @param name The name of the pixbuf file to load.
 *
 *  @return A pointer to the pixbuf, or NULL of the file couldn't
 *  be found or loaded..
 */
GdkPixbuf * gnc_gnome_get_gdkpixbuf (const char *name);


/** Shutdown gnucash.  This function will call the Scheme side of
 *  GnuCash to initiate an orderly shutdown, and when that has
 *  finished it will exit the program.
 *
 *  @param exit_status The exit status for the program.
 */
void gnc_shutdown (int exit_status);


#endif
/** @} */
/** @} */