/********************************************************************\
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

/*
 * The Gnucash Sheet widget
 *
 *  Based heavily on the Gnumeric Sheet widget.
 *
 * Author:
 *     Heath Martin <martinh@pegasus.cc.ucf.edu>
 *     Dave Peticolas <dave@krondo.com>
 */


#include "gnucash-sheet.h"

#include "gnucash-color.h"
#include "gnucash-grid.h"
#include "gnucash-cursor.h"
#include "gnucash-style.h"
#include "gnucash-header.h"
#include "gnucash-item-edit.h"
#include "util.h"

#define DEFAULT_REGISTER_HEIGHT 400
#define DEFAULT_REGISTER_WIDTH  630

static guint gnucash_register_initial_rows = 15;

static void gnucash_sheet_start_editing_at_cursor (GnucashSheet *sheet);

static gboolean gnucash_sheet_cursor_move (GnucashSheet *sheet,
                                           VirtualLocation virt_loc);

static void gnucash_sheet_deactivate_cursor_cell (GnucashSheet *sheet);
static void gnucash_sheet_activate_cursor_cell (GnucashSheet *sheet,
                                                gboolean changed_cells);
static void gnucash_sheet_stop_editing (GnucashSheet *sheet);
static gint gnucash_sheet_row_get_distance (GnucashSheet *sheet,
                                            int v_row_a, int v_row_b);


/* Register signals */
enum
{
        ACTIVATE_CURSOR,
        LAST_SIGNAL
};

static GnomeCanvasClass *sheet_parent_class;
static GtkTableClass *register_parent_class;
static guint register_signals[LAST_SIGNAL];


void
gnucash_sheet_set_cursor (GnucashSheet *sheet, CellBlock *cursor,
                          GNCCursorType cursor_type)
{
        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET (sheet));
        g_return_if_fail (cursor_type >= 0);
        g_return_if_fail (cursor_type < GNUCASH_NUM_CURSORS);

        sheet->cursors[cursor_type] = cursor;
}

void
gnucash_register_set_initial_rows(guint num_rows)
{
        gnucash_register_initial_rows = num_rows;
}

G_INLINE_FUNC gboolean
gnucash_sheet_virt_cell_out_of_bounds (GnucashSheet *sheet,
                                       VirtualCellLocation vcell_loc);
G_INLINE_FUNC gboolean
gnucash_sheet_virt_cell_out_of_bounds (GnucashSheet *sheet,
                                       VirtualCellLocation vcell_loc)
{
        return (vcell_loc.virt_row < 1 ||
                vcell_loc.virt_row >= sheet->num_virt_rows ||
                vcell_loc.virt_col < 0 ||
                vcell_loc.virt_col >= sheet->num_virt_cols);
}

static gboolean
gnucash_sheet_cell_valid (GnucashSheet *sheet, VirtualLocation virt_loc)
{
        gboolean valid;
        SheetBlockStyle *style;

        valid = !gnucash_sheet_virt_cell_out_of_bounds (sheet,
                                                        virt_loc.vcell_loc);

        if (valid) {
                style = gnucash_sheet_get_style (sheet, virt_loc.vcell_loc);

                valid = (virt_loc.phys_row_offset >= 0 &&
                         virt_loc.phys_row_offset < style->nrows &&
                         virt_loc.phys_col_offset >= 0 &&
                         virt_loc.phys_col_offset < style->ncols);
        }

        return valid;
}


void
gnucash_sheet_cursor_set (GnucashSheet *sheet, VirtualLocation virt_loc)
{
        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET (sheet));

        g_return_if_fail (virt_loc.vcell_loc.virt_row >= 0 ||
                          virt_loc.vcell_loc.virt_row <= sheet->num_virt_rows);
        g_return_if_fail (virt_loc.vcell_loc.virt_col >= 0 ||
                          virt_loc.vcell_loc.virt_col <= sheet->num_virt_cols);

        gnucash_cursor_set (GNUCASH_CURSOR(sheet->cursor), virt_loc);
}

void
gnucash_sheet_cursor_set_from_table (GnucashSheet *sheet, gboolean do_scroll)
{
        Table *table;
        VirtualLocation v_loc;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));

        table = sheet->table;
        v_loc = table->current_cursor_loc;

        gnucash_sheet_cursor_set(sheet, v_loc);

        if (do_scroll)
                gnucash_sheet_make_cell_visible(sheet, v_loc);
}


static void
gnucash_sheet_hide_editing_cursor (GnucashSheet *sheet)
{
        if (sheet->item_editor == NULL)
                return;

        gnome_canvas_item_hide(GNOME_CANVAS_ITEM (sheet->item_editor));
        item_edit_hide_list(ITEM_EDIT(sheet->item_editor));
}

static void
gnucash_sheet_stop_editing (GnucashSheet *sheet)
{

        if (sheet->insert_signal  > 0)
                gtk_signal_disconnect (GTK_OBJECT(sheet->entry),
				       sheet->insert_signal);
        if (sheet->delete_signal  > 0)
                gtk_signal_disconnect (GTK_OBJECT(sheet->entry),
				       sheet->delete_signal);

        sheet->insert_signal = 0;
        sheet->delete_signal = 0;

        gnucash_sheet_hide_editing_cursor (sheet);

        sheet->editing = FALSE;
}


static void
gnucash_sheet_deactivate_cursor_cell (GnucashSheet *sheet)
{
        VirtualLocation virt_loc;

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        gnucash_sheet_stop_editing (sheet);

        gnc_table_leave_update(sheet->table, virt_loc);

        gnucash_sheet_redraw_block (sheet, virt_loc.vcell_loc);
}


static void
gnucash_sheet_activate_cursor_cell (GnucashSheet *sheet,
                                    gboolean changed_cells)
{
        Table *table = sheet->table;
        VirtualLocation virt_loc;
        SheetBlockStyle *style;
        GtkEditable *editable;
        int cursor_pos, start_sel, end_sel;
        gboolean allow_edits;

        /* Sanity check */
        if (sheet->editing)
                gnucash_sheet_deactivate_cursor_cell (sheet);

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        /* This should be a no-op */
        gnc_table_wrap_verify_cursor_position (table, virt_loc);

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        style = gnucash_sheet_get_style (sheet, virt_loc.vcell_loc);
        if (style->cursor_type == GNUCASH_CURSOR_HEADER ||
            !gnc_table_virtual_loc_valid (table, virt_loc, TRUE))
                return;

        editable = GTK_EDITABLE(sheet->entry);

        cursor_pos = -1;
        start_sel = 0;
        end_sel = 0;

        allow_edits = gnc_table_enter_update (table, virt_loc, &cursor_pos,
                                              &start_sel, &end_sel);

	if (!allow_edits)
                gnucash_sheet_redraw_block (sheet, virt_loc.vcell_loc);
	else
        {
		gnucash_sheet_start_editing_at_cursor (sheet);

                gtk_editable_set_position(editable, cursor_pos);

                gtk_entry_select_region(GTK_ENTRY(sheet->entry),
                                        start_sel, end_sel);
        }

        gtk_widget_grab_focus (GTK_WIDGET(sheet));
}


static gboolean
gnucash_sheet_cursor_move (GnucashSheet *sheet, VirtualLocation virt_loc)
{
        VirtualLocation old_virt_loc;
        gboolean changed_cells;
        Table *table;

        table = sheet->table;

        /* Get the old cursor position */
        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &old_virt_loc);

        /* Turn off the editing controls */
        gnucash_sheet_deactivate_cursor_cell (sheet);

        /* Do the move. This may result in table restructuring due to
         * commits, auto modes, etc. */
        gnc_table_wrap_verify_cursor_position (table, virt_loc);

        /* A complete reload can leave us with editing back on */
        if (sheet->editing)
                gnucash_sheet_deactivate_cursor_cell (sheet);

        /* Find out where we really landed. We have to get the new
         * physical position as well, as the table may have been
         * restructured. */
        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        gnucash_sheet_cursor_set (sheet, virt_loc);

        /* We should be at our new location now. Show it on screen and
         * configure the cursor. */
        gnucash_sheet_make_cell_visible (sheet, virt_loc);

        changed_cells = !virt_loc_equal (virt_loc, old_virt_loc);

        /* Now turn on the editing controls. */
        gnucash_sheet_activate_cursor_cell (sheet, changed_cells);

        return changed_cells;
}


/* assumes a valid top block */
void
gnucash_sheet_compute_visible_range (GnucashSheet *sheet)
{
        Table *table;
        VirtualCellLocation vcell_loc;
        gint height;
        gint cy;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET (sheet));

        table = sheet->table;
        height = GTK_WIDGET(sheet)->allocation.height;

        gnome_canvas_get_scroll_offsets (GNOME_CANVAS(sheet), NULL, &cy);

        vcell_loc.virt_row = sheet->top_block;
        vcell_loc.virt_col = 0;

        for ( ;
              vcell_loc.virt_row < sheet->num_virt_rows - 1;
              vcell_loc.virt_row++ )
        {
                SheetBlock *block;

                block = gnucash_sheet_get_block (sheet, vcell_loc);

                if (block->origin_y - cy + block->style->dimensions->height
                    >= height)
                        break;
        }

        sheet->bottom_block = vcell_loc.virt_row;

        /* FIXME */
        sheet->left_block = 0;
        sheet->right_block = 0;
}


void
gnucash_sheet_block_pixel_origin (GnucashSheet *sheet,
                                  VirtualCellLocation vcell_loc,
				  gint *x, gint *y)
{
        SheetBlock *block;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));

        if (gnucash_sheet_virt_cell_out_of_bounds (sheet, vcell_loc))
                return;

        block = gnucash_sheet_get_block (sheet, vcell_loc);

        if (x)
                *x = block->origin_x;
        if (y)
                *y = block->origin_y;
}


static void
gnucash_sheet_set_top_row (GnucashSheet *sheet, gint new_top_row, gint align)
{
        VirtualCellLocation new_row_loc = { new_top_row, 0 };
        gint x, y;
        gint cx, cy;
        gint diff = 0;
        gint height;
        gint distance;
        gint last_visible;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));

        new_row_loc.virt_row = MAX (new_row_loc.virt_row, 1);
        new_row_loc.virt_row = MIN (new_row_loc.virt_row,
                                    sheet->num_virt_rows - 1);

        last_visible = new_row_loc.virt_row;

        if (align != GNUCASH_ALIGN_SAME)
                sheet->alignment = align;

        gnome_canvas_get_scroll_offsets (GNOME_CANVAS(sheet), &cx, &cy);

        x = cx;

        height = GTK_WIDGET(sheet)->allocation.height;
        distance = gnucash_sheet_row_get_distance(sheet, new_row_loc.virt_row,
                                                  sheet->num_virt_rows);

        while ((new_row_loc.virt_row > 1) && height > distance)
        {
                SheetBlock *block;

                new_row_loc.virt_row--;

                block = gnucash_sheet_get_block (sheet, new_row_loc);
                if (!block->visible)
                        continue;

                last_visible = new_row_loc.virt_row;

                distance += block->style->dimensions->height;
        }

        new_row_loc.virt_row = last_visible;

        gnucash_sheet_block_pixel_origin (sheet, new_row_loc, NULL, &y);

        if (sheet->alignment == GNUCASH_ALIGN_BOTTOM) {
                VirtualCellLocation vcell_loc = { sheet->bottom_block, 0 };
                SheetBlock *block;

                distance = gnucash_sheet_row_get_distance
                        (sheet, sheet->top_block, sheet->bottom_block);

                block = gnucash_sheet_get_block (sheet, vcell_loc);
                if (block)
                        distance += block->style->dimensions->height;

                if (distance > height)
                        diff = distance - height;
        }

        y += diff;

        sheet->top_block_offset = -diff;

        sheet->top_block = last_visible;

        if (x != cx || y != cy) {
                gnucash_sheet_compute_visible_range(sheet);
                gnome_canvas_update_now (GNOME_CANVAS(sheet));

                if (y != cy)
                        gtk_adjustment_set_value(sheet->vadj, y);
                if (x != cx)
                        gtk_adjustment_set_value(sheet->hadj, x);
        }

        gnucash_sheet_update_adjustments (sheet);        
}


void
gnucash_sheet_make_cell_visible (GnucashSheet *sheet, VirtualLocation virt_loc)
{
        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET (sheet));

        if (!gnucash_sheet_cell_valid (sheet, virt_loc))
                return;

        if (virt_loc.vcell_loc.virt_row <= sheet->top_block)
                gnucash_sheet_set_top_row (sheet, virt_loc.vcell_loc.virt_row,
                                           GNUCASH_ALIGN_TOP);
        else if (virt_loc.vcell_loc.virt_row >= sheet->bottom_block)
                /* FIXME -- invisible rows */
                gnucash_sheet_set_top_row (sheet,
                                           sheet->top_block +
                                           (virt_loc.vcell_loc.virt_row -
                                            sheet->bottom_block),
                                           GNUCASH_ALIGN_BOTTOM);

        if (!sheet->smooth_scroll) 
                sheet->vadj->value = sheet->top_block;

        gnucash_sheet_update_adjustments (sheet);
}


void
gnucash_sheet_update_adjustments (GnucashSheet *sheet)
{
        GtkAdjustment *vadj;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET (sheet));
        g_return_if_fail (sheet->vadj != NULL);

        vadj = sheet->vadj;

        if (!sheet->smooth_scroll) {
                vadj->lower = 1;
                vadj->upper = MAX(sheet->bottom_block, sheet->num_virt_rows);
                vadj->page_size = sheet->bottom_block - sheet->top_block + 1;
                vadj->page_increment = vadj->page_size - 1;
                vadj->step_increment = 1;
        }
        else
                vadj->step_increment = vadj->page_size /
                        (sheet->bottom_block - sheet->top_block + 1);

        gtk_adjustment_changed(vadj);
}


static gint
gnucash_sheet_y_pixel_to_block (GnucashSheet *sheet, int y)
{
        VirtualCellLocation vcell_loc = { 1, 0 };

        for (;
             vcell_loc.virt_row < sheet->num_virt_rows - 1;
             vcell_loc.virt_row++)
        {
                SheetBlock *block;

                block = gnucash_sheet_get_block (sheet, vcell_loc);
                if (!block || !block->visible)
                        continue;

                if (block->origin_y + block->style->dimensions->height > y)
                        break;
        }

        return vcell_loc.virt_row;
}


static void
gnucash_sheet_vadjustment_value_changed (GtkAdjustment *adj,
					 GnucashSheet *sheet)
{
        gint new_top_row;
        gint oy;

        if (sheet->smooth_scroll) {
                VirtualCellLocation vcell_loc;

                new_top_row =
                        gnucash_sheet_y_pixel_to_block (sheet,
                                                        (gint) adj->value);

                sheet->top_block = new_top_row;

                vcell_loc.virt_row = new_top_row;
                vcell_loc.virt_col = 0;

                gnucash_sheet_block_pixel_origin (sheet, vcell_loc, NULL, &oy);
                sheet->top_block_offset = oy - (gint)adj->value;

                gnucash_sheet_compute_visible_range(sheet);
        }
        else {
                new_top_row = (gint) adj->value;

                if (new_top_row == sheet->top_block)
                        return;

                gnucash_sheet_set_top_row (sheet, new_top_row,
                                           GNUCASH_ALIGN_SAME);
        }
}


static gint
gnucash_sheet_row_get_distance (GnucashSheet *sheet, int v_row_a, int v_row_b)
{
        SheetBlock *block;
        VirtualCellLocation vcell_loc;
        int distance;
        int sign = 1;
        int a, b;

        g_return_val_if_fail (sheet != NULL, 0);
        g_return_val_if_fail (GNUCASH_IS_SHEET (sheet), 0);

        if (v_row_a < v_row_b) {
                a = v_row_a;
                b = v_row_b;
        }
        else {
                a = v_row_b;
                b = v_row_a;
                sign = -1;
        }

        a = MAX (a, 1);
        b = MIN (b, sheet->num_virt_rows);

        if (a == b)
                return 0;

        vcell_loc.virt_row = MIN (b, sheet->num_virt_rows - 1);
        vcell_loc.virt_col = 0;
        block = gnucash_sheet_get_block (sheet, vcell_loc);
        distance = block->origin_y;
        if (b == sheet->num_virt_rows)
                distance += block->style->dimensions->height;

        vcell_loc.virt_row = a;
        vcell_loc.virt_col = 0;
        block = gnucash_sheet_get_block (sheet, vcell_loc);
        distance -= block->origin_y;

        return sign * distance;
}

#if 0
static gint
gnucash_sheet_col_get_distance (GnucashSheet *sheet, int vrow,
                                int v_col_a, int v_col_b)
{
        SheetBlock *block;
        VirtualCellLocation vcell_loc;
        int distance;
        int sign = 1;
        int a, b;

        g_return_val_if_fail (sheet != NULL, 0);
        g_return_val_if_fail (GNUCASH_IS_SHEET (sheet), 0);
        g_return_val_if_fail (vrow >= 0 && vrow < sheet->num_virt_rows, 0);

        if (v_col_a < v_col_b) {
                a = v_col_a;
                b = v_col_b;
        }
        else {
                a = v_col_b;
                b = v_col_a;
                sign = -1;
        }

        a = MAX (a, 0);
        b = MIN (b, sheet->num_virt_cols);

        if (a == b)
                return 0;

        vcell_loc.virt_row = vrow;
        vcell_loc.virt_col = MIN (b, sheet->num_virt_cols - 1);
        block = gnucash_sheet_get_block (sheet, vcell_loc);
        distance = block->origin_x;
        if (b == sheet->num_virt_cols)
                distance += block->style->dimensions->width;

        vcell_loc.virt_row = vrow;
        vcell_loc.virt_col = a;
        block = gnucash_sheet_get_block (sheet, vcell_loc);
        distance -= block->origin_x;

        return sign * distance;
}
#endif

void
gnucash_sheet_redraw_all (GnucashSheet *sheet)
{
        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));

        gnome_canvas_request_redraw (GNOME_CANVAS (sheet), 0, 0,
                                     sheet->width + 1, sheet->height + 1);
}


void
gnucash_sheet_redraw_block (GnucashSheet *sheet, VirtualCellLocation vcell_loc)
{
        gint x, y, w, h;
        GnomeCanvas *canvas;
        SheetBlock *block;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));

        canvas = GNOME_CANVAS(sheet);

        block = gnucash_sheet_get_block (sheet, vcell_loc);
        if (!block || !block->style)
                return;

        x = block->origin_x;
        y = block->origin_y;

        h = block->style->dimensions->height;
        w = MIN(block->style->dimensions->width,
                GTK_WIDGET(sheet)->allocation.width);

        gnome_canvas_request_redraw (canvas, x, y, x+w+1, y+h+1);
}


static void
gnucash_sheet_destroy (GtkObject *object)
{
        GnucashSheet *sheet;
        gint i;

        sheet = GNUCASH_SHEET (object);

        g_table_destroy (sheet->blocks);
        sheet->blocks = NULL;

        for (i = GNUCASH_CURSOR_HEADER; i < GNUCASH_NUM_CURSORS; i++)
                gnucash_sheet_style_destroy(sheet, sheet->cursor_styles[i]);

        g_hash_table_destroy (sheet->dimensions_hash_table);        

        if (GTK_OBJECT_CLASS (sheet_parent_class)->destroy)
                (*GTK_OBJECT_CLASS (sheet_parent_class)->destroy)(object);

	/* This has to come after the parent destroy, so the item edit
	   destruction can do its disconnects. */
        gtk_widget_unref (sheet->entry);
}


static void
gnucash_sheet_realize (GtkWidget *widget)
{
        GdkWindow *window;

        if (GTK_WIDGET_CLASS (sheet_parent_class)->realize)
                (*GTK_WIDGET_CLASS (sheet_parent_class)->realize)(widget);

        window = widget->window;
        gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window,
				    NULL, FALSE);
}


static GnucashSheet *
gnucash_sheet_create (Table *table)
{
        GnucashSheet *sheet;
        GnomeCanvas  *canvas;

        sheet = gtk_type_new (gnucash_sheet_get_type ());
        canvas = GNOME_CANVAS (sheet);

        sheet->table = table;
        sheet->entry = NULL;

        if (sheet->smooth_scroll)
                sheet->vadj = gtk_layout_get_vadjustment (GTK_LAYOUT(canvas));
        else
                sheet->vadj = GTK_ADJUSTMENT(gtk_adjustment_new (0.0, 0.0, 0.0,
                                                                 1.0, 1.0, 1.0));

        sheet->hadj = gtk_layout_get_hadjustment (GTK_LAYOUT(canvas));

        gtk_signal_connect(GTK_OBJECT(sheet->vadj), "value_changed",
			   GTK_SIGNAL_FUNC(gnucash_sheet_vadjustment_value_changed), sheet);

        return sheet;
}

static gint
compute_optimal_width (GnucashSheet *sheet)
{
        SheetBlockStyle *style;

        if ((sheet == NULL) ||
            (sheet->cursor_styles[GNUCASH_CURSOR_HEADER] == NULL))
                return DEFAULT_REGISTER_WIDTH;

        if (sheet->window_width >= 0)
                return sheet->window_width;

        style = sheet->cursor_styles[GNUCASH_CURSOR_HEADER];

        if ((style == NULL) || (style->dimensions == NULL))
                return DEFAULT_REGISTER_WIDTH;

        return style->dimensions->width;
}


/* Compute the height needed to show DEFAULT_REGISTER_ROWS rows */
static gint
compute_optimal_height (GnucashSheet *sheet)
{
        SheetBlockStyle *style;
        CellDimensions *cd;
        gint row_height;

        if (sheet->window_height >= 0)
                return sheet->window_height;

        if ((sheet == NULL) ||
            (sheet->cursor_styles[GNUCASH_CURSOR_HEADER] == NULL))
                return DEFAULT_REGISTER_HEIGHT;

        style = sheet->cursor_styles[GNUCASH_CURSOR_HEADER];

        cd = gnucash_style_get_cell_dimensions (style, 0, 0);
        if (cd == NULL)
                return DEFAULT_REGISTER_HEIGHT;

        row_height = cd->pixel_height;

        return row_height * gnucash_register_initial_rows;
}


static void
gnucash_sheet_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
        GnucashSheet *sheet = GNUCASH_SHEET(widget);

        requisition->width = compute_optimal_width (sheet);
        requisition->height = compute_optimal_height (sheet);
}

const char *
gnucash_sheet_modify_current_cell(GnucashSheet *sheet, const gchar *new_text)
{
        GtkEditable *editable;
        Table *table = sheet->table;
        VirtualLocation virt_loc;

        const char *retval;

        int cursor_position, start_sel, end_sel;

        gnucash_cursor_get_virt(GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        if (!gnc_table_virtual_loc_valid (table, virt_loc, TRUE))
                return NULL;

        editable = GTK_EDITABLE(sheet->entry);

        cursor_position = editable->current_pos;
        start_sel = MIN(editable->selection_start_pos,
                        editable->selection_end_pos);
        end_sel = MAX(editable->selection_start_pos,
                      editable->selection_end_pos);

        retval = gnc_table_modify_update (table, virt_loc,
					  new_text, new_text,
                                          &cursor_position,
                                          &start_sel, &end_sel);

        if (retval) {
                gtk_signal_handler_block (GTK_OBJECT (sheet->entry),
					  sheet->insert_signal);

                gtk_signal_handler_block (GTK_OBJECT (sheet->entry),
					  sheet->delete_signal);

                gtk_entry_set_text (GTK_ENTRY (sheet->entry), retval);

                gtk_signal_handler_unblock (GTK_OBJECT (sheet->entry),
					  sheet->delete_signal);

                gtk_signal_handler_unblock (GTK_OBJECT (sheet->entry),
					    sheet->insert_signal);
        }

        gtk_editable_set_position (editable, cursor_position);
        gtk_entry_select_region(GTK_ENTRY(sheet->entry), start_sel, end_sel);

	return retval;
}


static void
gnucash_sheet_insert_cb (GtkWidget *widget,
                         const gchar *new_text,
                         const gint new_text_length,
                         gint *position,
                         GnucashSheet *sheet)
{
        GtkEditable *editable;
        Table *table = sheet->table;
        VirtualLocation virt_loc;

        const char *old_text;
        char *newval = NULL;
        char *change = NULL;
        const char *retval;

        int start_sel, end_sel;
        int old_position;
        int old_len;

        if (!new_text_length)
                return;

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        if (!gnc_table_virtual_loc_valid (table, virt_loc, FALSE))
                return;

        old_text = gtk_entry_get_text (GTK_ENTRY(sheet->entry));
        if (old_text == NULL)
                old_text = "";
        old_len = strlen(old_text);
        old_position = *position;

        /* we set newval to what the entry contents would be if
           the insert was processed */
        newval = g_new0(char, strlen(old_text) + new_text_length + 1);

        strncat (newval, old_text, *position);
        strncat (newval, new_text, new_text_length);
        strncat (newval, &old_text[*position], old_len - *position);

        change = g_new0 (char, new_text_length  + 1);
        strncpy (change, new_text, new_text_length);

        editable = GTK_EDITABLE(sheet->entry);

        start_sel = MIN(editable->selection_start_pos,
                        editable->selection_end_pos);
        end_sel = MAX(editable->selection_start_pos,
                      editable->selection_end_pos);

        retval = gnc_table_modify_update (table, virt_loc, change, newval,
                                          position, &start_sel, &end_sel);

        if (retval &&
            ((safe_strcmp (retval, newval) != 0) ||
             (*position != old_position))) {
                gtk_signal_handler_block(GTK_OBJECT (sheet->entry),
					 sheet->insert_signal);

                gtk_signal_handler_block(GTK_OBJECT (sheet->entry),
                                         sheet->delete_signal);

                gtk_entry_set_text (GTK_ENTRY (sheet->entry), retval);

                gtk_signal_handler_unblock(GTK_OBJECT (sheet->entry),
                                           sheet->delete_signal);

                gtk_signal_handler_unblock (GTK_OBJECT (sheet->entry),
                                            sheet->insert_signal);

                gtk_signal_emit_stop_by_name (GTK_OBJECT(sheet->entry),
                                              "insert_text");
        }
        else if (retval == NULL) {
                retval = old_text;

                /* the entry was disallowed, so we stop the insert signal */
                gtk_signal_emit_stop_by_name (GTK_OBJECT(sheet->entry),
                                              "insert_text");
        }

        if (*position < 0)
                *position = strlen(retval);

        gtk_entry_select_region(GTK_ENTRY(sheet->entry), start_sel, end_sel);

        g_free (change);
        g_free (newval);
}


static void
gnucash_sheet_delete_cb (GtkWidget *widget,
                         const gint start_pos,
                         const gint end_pos,
                         GnucashSheet *sheet)
{
        GtkEditable *editable;
        Table *table = sheet->table;
        VirtualLocation virt_loc;

        const char *old_text;
        char *newval = NULL;
        const char *retval = NULL;

        int cursor_position = start_pos;
        int start_sel, end_sel;

        if (end_pos <= start_pos)
                return;

        gnucash_cursor_get_virt (GNUCASH_CURSOR (sheet->cursor), &virt_loc);

        if (!gnc_table_virtual_loc_valid (table, virt_loc, FALSE))
                return;

        old_text = gtk_entry_get_text (GTK_ENTRY(sheet->entry));

        if (old_text == NULL)
                old_text = "";

        newval = g_new0 (char, strlen(old_text) - (end_pos - start_pos) + 1);

        strncat (newval, old_text, start_pos);
        strcat (newval, &old_text[end_pos]);

        editable = GTK_EDITABLE(sheet->entry);

        start_sel = MIN(editable->selection_start_pos,
                        editable->selection_end_pos);
        end_sel = MAX(editable->selection_start_pos,
                      editable->selection_end_pos);

        retval = gnc_table_modify_update (table, virt_loc,
                                          NULL, newval,
                                          &cursor_position,
                                          &start_sel, &end_sel);

        if (retval && (safe_strcmp (retval, newval) != 0)) {
                gtk_signal_handler_block (GTK_OBJECT (sheet->entry),
                                          sheet->insert_signal);

                gtk_signal_handler_block(GTK_OBJECT (sheet->entry),
					 sheet->delete_signal);

                gtk_entry_set_text (GTK_ENTRY (sheet->entry), retval);

                gtk_signal_handler_unblock(GTK_OBJECT (sheet->entry),
					 sheet->delete_signal);

                gtk_signal_handler_unblock (GTK_OBJECT (sheet->entry),
                                            sheet->insert_signal);

                gtk_signal_emit_stop_by_name (GTK_OBJECT(sheet->entry),
                                              "delete_text");
        }
        else if (retval == NULL) {
                /* the entry was disallowed, so we stop the delete signal */
                gtk_signal_emit_stop_by_name (GTK_OBJECT(sheet->entry),
                                              "delete_text");
        }

        gtk_editable_set_position (editable, cursor_position);
        gtk_entry_select_region(GTK_ENTRY(sheet->entry), start_sel, end_sel);

        g_free (newval);
}


static void
gnucash_sheet_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
        GnucashSheet *sheet = GNUCASH_SHEET(widget);

        if (GTK_WIDGET_CLASS(sheet_parent_class)->size_allocate)
                (*GTK_WIDGET_CLASS (sheet_parent_class)->size_allocate)
                        (widget, allocation);

        if (allocation->height == sheet->window_height &&
            allocation->width == sheet->window_width)
                return;

        sheet->window_height = allocation->height;
        sheet->window_width = allocation->width;

        gnucash_sheet_styles_set_dimensions(sheet, allocation->width);

        gnucash_cursor_configure (GNUCASH_CURSOR (sheet->cursor));
        gnucash_header_reconfigure (GNUCASH_HEADER(sheet->header_item));
        gnucash_sheet_set_scroll_region (sheet);

        item_edit_configure (ITEM_EDIT(sheet->item_editor));
        gnucash_sheet_update_adjustments (sheet);
}


static void
gnucash_sheet_start_editing_at_cursor (GnucashSheet *sheet)
{
        GnomeCanvas *canvas;
        const char *text;
        VirtualLocation virt_loc;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET (sheet));

        canvas = GNOME_CANVAS(sheet);

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        text = gnc_table_get_entry (sheet->table, virt_loc);

        item_edit_configure (ITEM_EDIT(sheet->item_editor));
        gnome_canvas_item_show (GNOME_CANVAS_ITEM (sheet->item_editor));

        gtk_entry_set_text (GTK_ENTRY(sheet->entry), text);

        sheet->editing = TRUE;

        /* set up the signals */
        sheet->insert_signal =
		gtk_signal_connect(GTK_OBJECT(sheet->entry), "insert_text",
				   GTK_SIGNAL_FUNC(gnucash_sheet_insert_cb),
                                   sheet);

        sheet->delete_signal =
		gtk_signal_connect(GTK_OBJECT(sheet->entry), "delete_text",
				   GTK_SIGNAL_FUNC(gnucash_sheet_delete_cb),
				   sheet);
}

static gboolean
gnucash_motion_event (GtkWidget *widget, GdkEventMotion *event)
{
        GnucashSheet *sheet;
        VirtualLocation virt_loc;
        int xoffset, yoffset, x;

        g_return_val_if_fail(widget != NULL, TRUE);
        g_return_val_if_fail(GNUCASH_IS_SHEET(widget), TRUE);
        g_return_val_if_fail(event != NULL, TRUE);

        sheet = GNUCASH_SHEET(widget);

        if (!(event->state & GDK_BUTTON1_MASK) && sheet->grabbed)
        {
                gtk_grab_remove (widget);
                sheet->grabbed = FALSE;
        }

        if (sheet->button != 1)
                return FALSE;

        if (!sheet->editing || event->type != GDK_MOTION_NOTIFY)
                return FALSE;

        if (!(event->state & GDK_BUTTON1_MASK))
                return FALSE;

        gnome_canvas_get_scroll_offsets (GNOME_CANVAS(sheet),
					 &xoffset, &yoffset);

        x = xoffset + event->x;

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        item_edit_set_cursor_pos (ITEM_EDIT(sheet->item_editor),
                                  virt_loc, x, FALSE, TRUE);

        return TRUE;
}

static gboolean
gnucash_button_release_event (GtkWidget *widget, GdkEventButton *event)
{
        GnucashSheet *sheet;

        g_return_val_if_fail(widget != NULL, TRUE);
        g_return_val_if_fail(GNUCASH_IS_SHEET(widget), TRUE);
        g_return_val_if_fail(event != NULL, TRUE);

        sheet = GNUCASH_SHEET (widget);

        if (sheet->button != event->button)
                return FALSE;

        sheet->button = 0;

        if (event->button != 1)
                return FALSE;

        gtk_grab_remove (widget);
        sheet->grabbed = FALSE;

        item_edit_set_has_selection(ITEM_EDIT(sheet->item_editor), FALSE);

        item_edit_claim_selection(ITEM_EDIT(sheet->item_editor), event->time);

        return TRUE;
}

static void
gnucash_sheet_scroll_event(GnucashSheet *sheet, GdkEventButton *event)
{
        GtkAdjustment *vadj;
        gfloat multiplier = 1.0;
        gfloat v_value;

        vadj = sheet->vadj;
        v_value = vadj->value;
        if (event->state & GDK_SHIFT_MASK)
                multiplier = 5.0;

        switch (event->button)
        {
                case 4:
                        v_value -= vadj->step_increment * multiplier;
                        break;
                case 5:
                        v_value += vadj->step_increment * multiplier;
                        break;
                default:
                        return;
        }

        v_value = CLAMP(v_value, vadj->lower, vadj->upper - vadj->page_size);

        gtk_adjustment_set_value(vadj, v_value);
}

static void
gnucash_sheet_check_grab (GnucashSheet *sheet)
{
        GdkModifierType mods;

        if (!sheet->grabbed)
                return;

        gdk_input_window_get_pointer(GTK_WIDGET(sheet)->window,
                                     GDK_CORE_POINTER, NULL, NULL,
                                     NULL, NULL, NULL, &mods);

        if (!(mods & GDK_BUTTON1_MASK))
        {
                gtk_grab_remove (GTK_WIDGET(sheet));
                sheet->grabbed = FALSE;
        }
}

static gboolean
gnucash_button_press_event (GtkWidget *widget, GdkEventButton *event)
{
        GnucashSheet *sheet;
        VirtualCell *vcell;
        int xoffset, yoffset;
        gboolean changed_cells;
        int x, y;

        VirtualLocation cur_virt_loc;
        VirtualLocation new_virt_loc;

        Table *table;
        gboolean exit_register;

        g_return_val_if_fail(widget != NULL, TRUE);
        g_return_val_if_fail(GNUCASH_IS_SHEET(widget), TRUE);
        g_return_val_if_fail(event != NULL, TRUE);

        sheet = GNUCASH_SHEET (widget);
        table = sheet->table;

        if (sheet->button && (sheet->button != event->button))
                return FALSE;

        sheet->button = event->button;

        if (!GTK_WIDGET_HAS_FOCUS(widget))
                gtk_widget_grab_focus(widget);

        switch (event->button)
        {
                case 1:
                        break;
                case 2:
                        if (event->type != GDK_BUTTON_PRESS)
                                return FALSE;
                        item_edit_paste_primary(ITEM_EDIT(sheet->item_editor),
                                                event->time);
                        return TRUE;
                case 3:
                        return FALSE;
                case 4:
                case 5:
                        gnucash_sheet_scroll_event(sheet, event);
                        return TRUE;
                default:
                        return FALSE;
        }

        gnome_canvas_get_scroll_offsets (GNOME_CANVAS(sheet),
					 &xoffset, &yoffset);

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &cur_virt_loc);

        x = xoffset + event->x;
        y = yoffset + event->y;

	if (!gnucash_grid_find_loc_by_pixel(GNUCASH_GRID(sheet->grid),
                                            x, y, &new_virt_loc))
		return TRUE;

        vcell = gnc_table_get_virtual_cell (table, new_virt_loc.vcell_loc);
        if (vcell == NULL)
                return TRUE;

        if (virt_loc_equal (new_virt_loc, cur_virt_loc) &&
            (event->type == GDK_2BUTTON_PRESS))
        {
                item_edit_set_cursor_pos (ITEM_EDIT(sheet->item_editor),
                                          cur_virt_loc, x, FALSE, FALSE);

                gtk_editable_set_position(GTK_EDITABLE(sheet->entry), -1);
                gtk_editable_select_region(GTK_EDITABLE(sheet->entry), 0, -1);

                item_edit_claim_selection (ITEM_EDIT(sheet->item_editor),
                                           event->time);

                return TRUE;
        }

        if (event->type != GDK_BUTTON_PRESS)
                return FALSE;

        gtk_grab_add(widget);
        sheet->grabbed = TRUE;

        item_edit_set_has_selection(ITEM_EDIT(sheet->item_editor), TRUE);

        if (virt_loc_equal (new_virt_loc, cur_virt_loc) && sheet->editing)
        {
                gboolean extend_selection = event->state & GDK_SHIFT_MASK;

                item_edit_set_cursor_pos (ITEM_EDIT(sheet->item_editor),
                                          cur_virt_loc, x, FALSE,
                                          extend_selection);

                return TRUE;
        }

        /* and finally...process this as a POINTER_TRAVERSE */
        exit_register = gnc_table_traverse_update (table,
                                                   cur_virt_loc,
                                                   GNC_TABLE_TRAVERSE_POINTER,
                                                   &new_virt_loc);

        gnucash_sheet_check_grab(sheet);

        if (exit_register)
		return TRUE;

        changed_cells = gnucash_sheet_cursor_move(sheet, new_virt_loc);

        gnucash_sheet_check_grab(sheet);

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &new_virt_loc);

        item_edit_set_cursor_pos (ITEM_EDIT(sheet->item_editor),
                                  new_virt_loc, x, changed_cells, FALSE);

        return TRUE;
}

void
gnucash_register_cut_clipboard (GnucashRegister *reg)
{
        GnucashSheet *sheet;
        ItemEdit *item_edit;

        g_return_if_fail(reg != NULL);
        g_return_if_fail(GNUCASH_IS_REGISTER(reg));

        sheet = GNUCASH_SHEET(reg->sheet);
        item_edit = ITEM_EDIT(sheet->item_editor);

        item_edit_cut_clipboard(item_edit, GDK_CURRENT_TIME);
}

void
gnucash_register_copy_clipboard (GnucashRegister *reg)
{
        GnucashSheet *sheet;
        ItemEdit *item_edit;

        g_return_if_fail(reg != NULL);
        g_return_if_fail(GNUCASH_IS_REGISTER(reg));

        sheet = GNUCASH_SHEET(reg->sheet);
        item_edit = ITEM_EDIT(sheet->item_editor);

        item_edit_copy_clipboard(item_edit, GDK_CURRENT_TIME);
}

void
gnucash_register_paste_clipboard (GnucashRegister *reg)
{
        GnucashSheet *sheet;
        ItemEdit *item_edit;

        g_return_if_fail(reg != NULL);
        g_return_if_fail(GNUCASH_IS_REGISTER(reg));

        sheet = GNUCASH_SHEET(reg->sheet);
        item_edit = ITEM_EDIT(sheet->item_editor);

        item_edit_paste_clipboard(item_edit, GDK_CURRENT_TIME);
}

static gboolean
gnucash_sheet_clipboard_event (GnucashSheet *sheet, GdkEventKey *event)
{
        ItemEdit *item_edit;
        gboolean handled = FALSE;
        guint32 time;

        item_edit = ITEM_EDIT(sheet->item_editor);
        time = event->time;

        switch (event->keyval) {
                case GDK_C:
                case GDK_c:
                        if (event->state & GDK_CONTROL_MASK)
                        {
                                item_edit_copy_clipboard(item_edit, time);
                                handled = TRUE;
                        }
                        break;
                case GDK_X:
                case GDK_x:
                        if (event->state & GDK_CONTROL_MASK)
                        {
                                item_edit_cut_clipboard(item_edit, time);
                                handled = TRUE;
                        }
                        break;
                case GDK_V:
                case GDK_v:
                        if (event->state & GDK_CONTROL_MASK)
                        {
                                item_edit_paste_clipboard(item_edit, time);
                                handled = TRUE;
                        }
                        break;
                case GDK_Insert:
                        if (event->state & GDK_SHIFT_MASK)
                        {
                                item_edit_paste_clipboard(item_edit, time);
                                handled = TRUE;
                        }
                        else if (event->state & GDK_CONTROL_MASK)
                        {
                                item_edit_copy_clipboard(item_edit, time);
                                handled = TRUE;
                        }
                        break;
        }

        return handled;
}

static gboolean
gnucash_sheet_direct_event(GnucashSheet *sheet, GdkEvent *event)
{
        GtkEditable *editable;
        Table *table = sheet->table;
        VirtualLocation virt_loc;
        gboolean changed;
        gboolean result;

        char *new_text = NULL;

        int cursor_position, start_sel, end_sel;
        int new_position, new_start, new_end;

        gnucash_cursor_get_virt(GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        if (!gnc_table_virtual_loc_valid (table, virt_loc, TRUE))
                return FALSE;

        editable = GTK_EDITABLE(sheet->entry);

        cursor_position = editable->current_pos;
        start_sel = MIN(editable->selection_start_pos,
                        editable->selection_end_pos);
        end_sel = MAX(editable->selection_start_pos,
                      editable->selection_end_pos);

        new_position = cursor_position;
        new_start = start_sel;
        new_end = end_sel;

        result = gnc_table_direct_update(table, virt_loc,
                                         &new_text,
                                         &new_position,
                                         &new_start, &new_end,
                                         event);

        changed = FALSE;

        if (new_text != NULL)
        {
                gtk_signal_handler_block (GTK_OBJECT (sheet->entry),
					  sheet->insert_signal);

                gtk_signal_handler_block (GTK_OBJECT (sheet->entry),
					  sheet->delete_signal);

                gtk_entry_set_text (GTK_ENTRY (sheet->entry), new_text);

                gtk_signal_handler_unblock (GTK_OBJECT (sheet->entry),
                                            sheet->delete_signal);

                gtk_signal_handler_unblock (GTK_OBJECT (sheet->entry),
					    sheet->insert_signal);

                changed = TRUE;
        }

        if (new_position != cursor_position)
        {
                gtk_editable_set_position (editable, new_position);
                changed = TRUE;
        }

        if ((new_start != start_sel) || (new_end != end_sel))
        {
                gtk_entry_select_region(GTK_ENTRY(sheet->entry),
                                        new_start, new_end);
                changed = TRUE;
        }

        if (changed)
                item_edit_redraw(ITEM_EDIT(sheet->item_editor));

	return result;
}

static gint
gnucash_sheet_key_press_event (GtkWidget *widget, GdkEventKey *event)
{
        Table *table;
        GnucashSheet *sheet;
	CellBlock *header;
	gboolean pass_on = FALSE;
        gboolean exit_register;
        gboolean set_selection = TRUE;
        VirtualLocation cur_virt_loc;
        VirtualLocation new_virt_loc;
	gncTableTraversalDir direction = 0;
        int distance;

        g_return_val_if_fail(widget != NULL, TRUE);
        g_return_val_if_fail(GNUCASH_IS_SHEET(widget), TRUE);
        g_return_val_if_fail(event != NULL, TRUE);

        sheet = GNUCASH_SHEET (widget);
        table = sheet->table;
	header = gnc_table_get_header_cell(table)->cellblock;

        if (gnucash_sheet_direct_event(sheet, (GdkEvent *) event))
            return TRUE;

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &cur_virt_loc);
        new_virt_loc = cur_virt_loc;

	/* Calculate tentative physical values */
        switch (event->keyval) {
                case GDK_Return:
                case GDK_KP_Enter:
                        gtk_signal_emit_by_name(GTK_OBJECT(sheet->reg),
                                                "activate_cursor");
                        return TRUE;
                        break;
		case GDK_Tab:
                case GDK_ISO_Left_Tab:
                        if (event->state & GDK_SHIFT_MASK) {
                                direction = GNC_TABLE_TRAVERSE_LEFT;
                                gnc_table_move_tab(table, &new_virt_loc, FALSE);
                        }
                        else {
                                direction = GNC_TABLE_TRAVERSE_RIGHT;
                                gnc_table_move_tab(table, &new_virt_loc, TRUE);
                        }
                        break;
                case GDK_KP_Page_Up:
		case GDK_Page_Up:
                        direction = GNC_TABLE_TRAVERSE_UP;
                        new_virt_loc.phys_col_offset = 0;
                        distance = sheet->bottom_block - sheet->top_block;
                        gnc_table_move_vertical_position (table, &new_virt_loc,
                                                          -distance);
			break;
                case GDK_KP_Page_Down:
		case GDK_Page_Down:
                        direction = GNC_TABLE_TRAVERSE_DOWN;
                        new_virt_loc.phys_col_offset = 0;
                        distance = sheet->bottom_block - sheet->top_block;
                        gnc_table_move_vertical_position (table, &new_virt_loc,
                                                          distance);
			break;
                case GDK_KP_Up:
		case GDK_Up:
			direction = GNC_TABLE_TRAVERSE_UP;
                        gnc_table_move_vertical_position (table,
                                                          &new_virt_loc, -1);
			break;
                case GDK_KP_Down:
		case GDK_Down:
                        if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))
                        {
                                ItemEdit *item_edit;

                                item_edit = ITEM_EDIT(sheet->item_editor);

                                item_edit_show_list(item_edit);

                                return TRUE;
                        }

			direction = GNC_TABLE_TRAVERSE_DOWN;
                        gnc_table_move_vertical_position (table,
                                                          &new_virt_loc, 1);
			break;
                case GDK_Control_L:
                case GDK_Control_R:
		case GDK_Shift_L:
		case GDK_Shift_R:
		case GDK_Alt_L:
		case GDK_Alt_R:
                        pass_on = TRUE;
                        set_selection = FALSE;
                        break;
		default:
                        if (gnucash_sheet_clipboard_event(sheet, event))
                                return TRUE;

			pass_on = TRUE;

                        /* This is a piece of logic from gtkentry.c. We
                           are trying to figure out whether to change the
                           selection. If this is a regular character, we
                           don't want to change the selection, as it will
                           get changed in the insert callback. */
                        if ((event->keyval >= 0x20) && (event->keyval <= 0xFF))
                        {
                                if (event->state & GDK_CONTROL_MASK)
                                        break;
                                if (event->state & GDK_MOD1_MASK)
                                        break;
                        }

                        if (event->length > 0)
                                set_selection = FALSE;

			break;
        }

	/* Forward the keystroke to the input line */
	if (pass_on)
        {
                GtkEditable *editable;
                gboolean extend_selection;
                gboolean result;
                gint current_pos;
                gint start_sel;
                gint end_sel;
                gint new_pos;

                editable = GTK_EDITABLE(sheet->entry);

                current_pos = editable->current_pos;

                extend_selection = event->state & GDK_SHIFT_MASK;
                if (extend_selection && set_selection)
                {
                        start_sel = MIN(editable->selection_start_pos,
                                        editable->selection_end_pos);
                        end_sel = MAX(editable->selection_start_pos,
                                      editable->selection_end_pos);
                }
                else
                {
                        start_sel = 0;
                        end_sel = 0;
                }

		result = gtk_widget_event(sheet->entry, (GdkEvent *) event);

                new_pos = editable->current_pos;

                if (extend_selection && set_selection)
                {
                        if (start_sel == end_sel)
                        {
                                start_sel = current_pos;
                                end_sel = new_pos;
                        }
                        else if (current_pos == start_sel)
                                start_sel = new_pos;
                        else
                                end_sel = new_pos;
                }

                if (set_selection)
                        gtk_entry_select_region(GTK_ENTRY(sheet->entry),
                                                start_sel, end_sel);

                return result;
        }

	exit_register = gnc_table_traverse_update (table, cur_virt_loc,
                                                   direction, &new_virt_loc);

	/* If that would leave the register, abort */
	if (exit_register)
                return TRUE;

	gnucash_sheet_cursor_move (sheet, new_virt_loc);

        /* return true because we handled the key press */
        return TRUE;
}


static void
gnucash_sheet_goto_virt_loc (GnucashSheet *sheet, VirtualLocation virt_loc)
{
        Table *table;
        gboolean exit_register;
        VirtualLocation cur_virt_loc;

        g_return_if_fail(GNUCASH_IS_SHEET(sheet));

        table = sheet->table;

        gnucash_cursor_get_virt (GNUCASH_CURSOR(sheet->cursor), &cur_virt_loc);

        /* It's not really a pointer traverse, but it seems the most
         * appropriate here. */
 	exit_register = gnc_table_traverse_update (table, cur_virt_loc,
                                                   GNC_TABLE_TRAVERSE_POINTER,
                                                   &virt_loc);

	if (exit_register)
		return;

	gnucash_sheet_cursor_move (sheet, virt_loc);
}


void
gnucash_register_goto_virt_cell (GnucashRegister *reg,
                                 VirtualCellLocation vcell_loc)
{
        GnucashSheet *sheet;
        VirtualLocation virt_loc;

        g_return_if_fail(reg != NULL);
        g_return_if_fail(GNUCASH_IS_REGISTER(reg));

        sheet = GNUCASH_SHEET(reg->sheet);

        virt_loc.vcell_loc = vcell_loc;
        virt_loc.phys_row_offset = 0;
        virt_loc.phys_col_offset = 0;

        gnucash_sheet_goto_virt_loc(sheet, virt_loc);
}

void
gnucash_register_goto_virt_loc (GnucashRegister *reg,
                                VirtualLocation virt_loc)
{
        GnucashSheet *sheet;

        g_return_if_fail(reg != NULL);
        g_return_if_fail(GNUCASH_IS_REGISTER(reg));

        sheet = GNUCASH_SHEET(reg->sheet);

        gnucash_sheet_goto_virt_loc(sheet, virt_loc);
}

void
gnucash_register_goto_next_virt_row (GnucashRegister *reg)
{
        GnucashSheet *sheet;
        VirtualLocation virt_loc;

        g_return_if_fail(GNUCASH_IS_REGISTER(reg));

        sheet = GNUCASH_SHEET(reg->sheet);

        gnucash_cursor_get_virt(GNUCASH_CURSOR(sheet->cursor), &virt_loc);

        virt_loc.vcell_loc.virt_row++;
        if (virt_loc.vcell_loc.virt_row >= sheet->num_virt_rows)
                return;

        virt_loc.phys_row_offset = 0;
        virt_loc.phys_col_offset = 0;

        gnucash_sheet_goto_virt_loc(sheet, virt_loc);
}


SheetBlock *
gnucash_sheet_get_block (GnucashSheet *sheet, VirtualCellLocation vcell_loc)
{
        g_return_val_if_fail (sheet != NULL, NULL);
        g_return_val_if_fail (GNUCASH_IS_SHEET(sheet), NULL);

        return g_table_index (sheet->blocks,
                              vcell_loc.virt_row,
                              vcell_loc.virt_col);
}


/* This fills up a block from the table; it sets the style and returns
 * true if the style changed. */
gboolean
gnucash_sheet_block_set_from_table (GnucashSheet *sheet,
                                    VirtualCellLocation vcell_loc)
{
        Table *table;
        SheetBlock *block;
        SheetBlockStyle *style;
        VirtualCell *vcell;

        block = gnucash_sheet_get_block (sheet, vcell_loc);
        style = gnucash_sheet_get_style_from_table (sheet, vcell_loc);

        if (block == NULL)
                return FALSE;

        table = sheet->table;

        vcell = gnc_table_get_virtual_cell (table, vcell_loc);
        if (vcell)
                block->visible = vcell->visible;

        if (block->style && (block->style != style)) {

                /* the zero'th virtual row isn't drawn */
                if (vcell_loc.virt_row > 0)
                        sheet->height -= block->style->dimensions->height;
                gnucash_style_unref (block->style);
                block->style = NULL;
        }

        if (block->style == NULL) {
                block->style = style;

                /* the zero'th virtual row isn't drawn */
                if (vcell_loc.virt_row > 0)
                        sheet->height += block->style->dimensions->height;

                gnucash_style_ref(block->style);

                return TRUE;
        }

        return FALSE;
}


gint
gnucash_sheet_col_max_width (GnucashSheet *sheet, gint virt_col, gint cell_col)
{
        int virt_row;
        int cell_row;
        int max = 0;
        int width;
        SheetBlock *block;
        SheetBlockStyle *style;
        GdkFont *font;

        g_return_val_if_fail (virt_col >= 0, 0);
        g_return_val_if_fail (virt_col < sheet->num_virt_cols, 0);
        g_return_val_if_fail (cell_col >= 0, 0);

        for (virt_row = 0; virt_row < sheet->num_virt_rows ; virt_row++) {
                VirtualCellLocation vcell_loc = { virt_row, virt_col };

                block = gnucash_sheet_get_block (sheet, vcell_loc);
                style = block->style;

                if (!style)
                        continue;

                if (cell_col < style->ncols) 
                        for (cell_row = 0; cell_row < style->nrows; cell_row++)
                        {
                                VirtualLocation virt_loc;
                                const char *text;

                                virt_loc.vcell_loc = vcell_loc;
                                virt_loc.phys_row_offset = cell_row;
                                virt_loc.phys_col_offset = cell_col;

                                if (virt_row == 0) {
                                        text = gnc_table_get_label
                                                (sheet->table, virt_loc);
                                        font = style->header_font;
                                }
                                else {
                                        text = gnc_table_get_entry
                                                (sheet->table, virt_loc);

                                        font = GNUCASH_GRID(sheet->grid)->normal_font;
                                }

                                width = (gdk_string_measure (font, text) +
                                         2 * CELL_HPADDING);

                                max = MAX (max, width);
                        }
        }

        return max;
}

void
gnucash_sheet_set_scroll_region (GnucashSheet *sheet)
{
        int height, width;
        GtkWidget *widget;
        double x, y;

        if (!sheet)
                return;

        widget = GTK_WIDGET(sheet);

        if (!sheet->header_item || !GNUCASH_HEADER(sheet->header_item)->style)
                return;

        gnome_canvas_get_scroll_region (GNOME_CANVAS(sheet),
                                        NULL, NULL, &x, &y);

        height = MAX (sheet->height, widget->allocation.height);
        width  = MAX (sheet->width, widget->allocation.width);

        if (width != (int)x || height != (int)y) 
                gnome_canvas_set_scroll_region (GNOME_CANVAS(sheet),
                                                0, 0, width, height);
}

static void
gnucash_sheet_block_destroy (gpointer _block, gpointer user_data)
{
        SheetBlock *block = _block;

        if (block == NULL)
                return;

        if (block->style) {
                gnucash_style_unref (block->style);
                block->style = NULL;
        }
}

static void
gnucash_sheet_block_construct (gpointer _block, gpointer user_data)
{
        SheetBlock *block = _block;

        block->style = NULL;
        block->visible = TRUE;
}

static void
gnucash_sheet_resize (GnucashSheet *sheet)
{
        gint i;
        gint diff_rows;
        gint num_virt_rows;
        VirtualCellLocation vcell_loc = { 0, 0 };

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));

        if (sheet->table->num_virt_cols > 1)
                g_warning ("num_virt_cols > 1");

        num_virt_rows = sheet->num_virt_rows;

        diff_rows = sheet->table->num_virt_rows - num_virt_rows;
        sheet->num_virt_cols = 1;

        if (diff_rows < 0) {
                for (i = 0; i < -diff_rows; i++) {
                        SheetBlock *block;

                        vcell_loc.virt_row = (num_virt_rows - i) - 1;

                        block = gnucash_sheet_get_block (sheet, vcell_loc);

                        sheet->height -= block->style->dimensions->height;
                }
        }

        g_table_resize (sheet->blocks, sheet->table->num_virt_rows, 1);

        sheet->num_virt_rows = sheet->table->num_virt_rows;
        gnucash_sheet_set_scroll_region (sheet);
}

void
gnucash_sheet_recompute_block_offsets (GnucashSheet *sheet)
{
        Table *table;
        SheetBlock *block;
        gint num_virt_rows;
        gint i, j;
        gint height;
        gint width;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));
        g_return_if_fail (sheet->table != NULL);

        table = sheet->table;
        num_virt_rows = table->num_virt_rows;

        height = 0;
        block = NULL;
        for (i = 0; i < table->num_virt_rows; i++) {
                width = 0;

                for (j = 0; j < table->num_virt_cols; j++) {
                        VirtualCellLocation vcell_loc = { i, j };

                        block = gnucash_sheet_get_block (sheet, vcell_loc);

                        block->origin_x = width;
                        block->origin_y = height;

                        width += block->style->dimensions->width;
                }

                if (i > 0)
                        height += block->style->dimensions->height;
        }
}

void
gnucash_sheet_table_load (GnucashSheet *sheet)
{
        Table *table;
        gint num_virt_rows;
        gint i, j;

        g_return_if_fail (sheet != NULL);
        g_return_if_fail (GNUCASH_IS_SHEET(sheet));
        g_return_if_fail (sheet->table != NULL);

        table = sheet->table;
        num_virt_rows = table->num_virt_rows;

        gtk_layout_freeze (GTK_LAYOUT(sheet));

        gnucash_sheet_stop_editing (sheet);

        /* resize the sheet */
        gnucash_sheet_resize (sheet);

        /* fill it up */
        for (i = 0; i < table->num_virt_rows; i++)
                for (j = 0; j < table->num_virt_cols; j++) {
                        VirtualCellLocation vcell_loc = { i, j };

                        gnucash_sheet_block_set_from_table (sheet, vcell_loc);
                }

        gnucash_sheet_recompute_block_offsets (sheet);

        gnucash_sheet_set_scroll_region (sheet);

        gnucash_sheet_set_top_row (sheet, sheet->top_block,
                                   GNUCASH_ALIGN_BOTTOM);

        gnucash_sheet_compute_visible_range(sheet);

        gnucash_sheet_cursor_set_from_table (sheet, TRUE);
        gnucash_sheet_activate_cursor_cell (sheet, TRUE);
        gtk_layout_thaw (GTK_LAYOUT(sheet));        
}

static gboolean
gnucash_sheet_selection_clear (GtkWidget          *widget,
                               GdkEventSelection  *event)
{
        GnucashSheet *sheet;

        g_return_val_if_fail(widget != NULL, FALSE);
        g_return_val_if_fail(GNUCASH_IS_SHEET(widget), FALSE);

        sheet = GNUCASH_SHEET(widget);

        return item_edit_selection_clear(ITEM_EDIT(sheet->item_editor), event);
}

static void
gnucash_sheet_selection_get (GtkWidget         *widget,
                             GtkSelectionData  *selection_data,
                             guint              info,
                             guint              time)
{
        GnucashSheet *sheet;

        g_return_if_fail(widget != NULL);
        g_return_if_fail(GNUCASH_IS_SHEET(widget));

        sheet = GNUCASH_SHEET(widget);

        item_edit_selection_get(ITEM_EDIT(sheet->item_editor),
                                selection_data, info, time);
}

static void
gnucash_sheet_selection_received (GtkWidget          *widget,
                                  GtkSelectionData   *selection_data,
                                  guint               time)
{
        GnucashSheet *sheet;

        g_return_if_fail(widget != NULL);
        g_return_if_fail(GNUCASH_IS_SHEET(widget));

        sheet = GNUCASH_SHEET(widget);

        item_edit_selection_received(ITEM_EDIT(sheet->item_editor),
                                     selection_data, time);
}

static void
gnucash_sheet_class_init (GnucashSheetClass *class)
{
        GtkObjectClass *object_class;
        GtkWidgetClass *widget_class;
        GnomeCanvasClass *canvas_class;

        object_class = (GtkObjectClass *) class;
        widget_class = (GtkWidgetClass *) class;
        canvas_class = (GnomeCanvasClass *) class;

        sheet_parent_class = gtk_type_class (gnome_canvas_get_type ());

        /* Method override */
        object_class->destroy = gnucash_sheet_destroy;

        widget_class->realize = gnucash_sheet_realize;

        widget_class->size_request = gnucash_sheet_size_request;
        widget_class->size_allocate = gnucash_sheet_size_allocate;

        widget_class->key_press_event = gnucash_sheet_key_press_event;
        widget_class->button_press_event = gnucash_button_press_event;
        widget_class->button_release_event = gnucash_button_release_event;
        widget_class->motion_notify_event = gnucash_motion_event;

        widget_class->selection_clear_event = gnucash_sheet_selection_clear;
        widget_class->selection_received = gnucash_sheet_selection_received;
        widget_class->selection_get = gnucash_sheet_selection_get;
}


static void
gnucash_sheet_init (GnucashSheet *sheet)
{
        GnomeCanvas *canvas = GNOME_CANVAS (sheet);

        GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
        GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);

        sheet->top_block = 1;
        sheet->bottom_block = 1;
        sheet->alignment = GNUCASH_ALIGN_TOP;

        sheet->top_block_offset = 0;

        sheet->num_virt_rows = 0;
        sheet->num_virt_cols = 0;
        sheet->item_editor = NULL;
        sheet->entry = NULL;
        sheet->editing = FALSE;
        sheet->button = 0;
        sheet->grabbed = FALSE;
        sheet->window_width = -1;
        sheet->window_height = -1;
        sheet->width = 0;
        sheet->height = 0;
        sheet->smooth_scroll = TRUE;

        sheet->blocks = g_table_new(sizeof (SheetBlock),
                                    gnucash_sheet_block_construct,
                                    gnucash_sheet_block_destroy, NULL);
}


GtkType
gnucash_sheet_get_type (void)
{
        static GtkType gnucash_sheet_type = 0;

        if (!gnucash_sheet_type){
                GtkTypeInfo gnucash_sheet_info = {
                        "GnucashSheet",
                        sizeof (GnucashSheet),
                        sizeof (GnucashSheetClass),
                        (GtkClassInitFunc) gnucash_sheet_class_init,
                        (GtkObjectInitFunc) gnucash_sheet_init,
                        NULL, /* reserved 1 */
                        NULL, /* reserved 2 */
                        (GtkClassInitFunc) NULL
                };

                gnucash_sheet_type =
			gtk_type_unique (gnome_canvas_get_type (),
					 &gnucash_sheet_info);
        }

        return gnucash_sheet_type;
}


GtkWidget *
gnucash_sheet_new (Table *table)
{
        GnucashSheet *sheet;
        GnomeCanvas *sheet_canvas;
        GnomeCanvasItem *item;
        GnomeCanvasGroup *sheet_group;

        g_return_val_if_fail (table != NULL, NULL);

        sheet = gnucash_sheet_create (table);

        /* handy shortcuts */
        sheet_canvas = GNOME_CANVAS (sheet);
        sheet_group = gnome_canvas_root (GNOME_CANVAS(sheet));

        /* The grid */
        item = gnome_canvas_item_new (sheet_group,
                                      gnucash_grid_get_type (),
                                      "GnucashGrid::Sheet", sheet,
                                      NULL);
        sheet->grid = item;

        /* some register data */
        sheet->dimensions_hash_table = g_hash_table_new (g_str_hash,
                                                         g_str_equal);

        /* The cursor */
        sheet->cursor = gnucash_cursor_new (sheet_group);
        gnome_canvas_item_set (sheet->cursor,
                               "GnucashCursor::sheet", sheet,
                               "GnucashCursor::grid", sheet->grid,
                               NULL);

        /* The entry widget */
        sheet->entry = gtk_entry_new ();
        gtk_widget_ref(sheet->entry);
        gtk_object_sink(GTK_OBJECT(sheet->entry));

        /* set up the editor */
        sheet->item_editor = item_edit_new(sheet_group, sheet, sheet->entry);

        gnome_canvas_item_hide (GNOME_CANVAS_ITEM(sheet->item_editor));

        return GTK_WIDGET(sheet);
}


static void
gnucash_register_class_init (GnucashRegisterClass *class)
{
        GtkObjectClass *object_class;
        GtkWidgetClass *widget_class;
        GtkTableClass *table_class;

        object_class = (GtkObjectClass *) class;
        widget_class = (GtkWidgetClass *) class;
        table_class = (GtkTableClass *) class;

        register_parent_class = gtk_type_class (gtk_table_get_type ());

        register_signals[ACTIVATE_CURSOR] =
                gtk_signal_new("activate_cursor",
                               GTK_RUN_LAST,
                               object_class->type,
                               GTK_SIGNAL_OFFSET(GnucashRegisterClass,
                                                 activate_cursor),
                               gtk_marshal_NONE__NONE,
                               GTK_TYPE_NONE, 0);

        gtk_object_class_add_signals(object_class, register_signals,
                                     LAST_SIGNAL);
}


static void
gnucash_register_init (GnucashRegister *g_reg)
{
        GtkTable *table = GTK_TABLE(g_reg);

        GTK_WIDGET_UNSET_FLAGS (table, GTK_CAN_FOCUS);
        GTK_WIDGET_UNSET_FLAGS (table, GTK_CAN_DEFAULT);

        gtk_table_set_homogeneous (table, FALSE);
        gtk_table_resize (table, 3, 2);
}


GtkType
gnucash_register_get_type (void)
{
        static GtkType gnucash_register_type = 0;

        if (!gnucash_register_type){
                GtkTypeInfo gnucash_register_info = {
                        "GnucashRegister",
                        sizeof (GnucashRegister),
                        sizeof (GnucashRegisterClass),
                        (GtkClassInitFunc) gnucash_register_class_init,
                        (GtkObjectInitFunc) gnucash_register_init,
                        NULL, /* reserved 1 */
                        NULL, /* reserved 2 */
                        (GtkClassInitFunc) NULL
                };

                gnucash_register_type = gtk_type_unique
			(gtk_table_get_type (),
			 &gnucash_register_info);
        }

        return gnucash_register_type;
}


void
gnucash_register_attach_popup(GnucashRegister *reg, GtkWidget *popup,
                              gpointer data)
{
        g_return_if_fail(GNUCASH_IS_REGISTER(reg));
        g_return_if_fail(GTK_IS_WIDGET(popup));
        g_return_if_fail(reg->sheet != NULL);

        gnome_popup_menu_attach(popup, reg->sheet, data);
}


GtkWidget *
gnucash_register_new (Table *table)
{
        GnucashRegister *reg;
        GtkWidget *header_canvas;
        GtkWidget *widget;
        GtkWidget *sheet;
        GtkWidget *scrollbar;

        reg = gtk_type_new(gnucash_register_get_type ());
        widget = GTK_WIDGET(reg);

        sheet = gnucash_sheet_new (table);
        reg->sheet = sheet;
        GNUCASH_SHEET(sheet)->reg = widget;

        header_canvas = gnucash_header_new (GNUCASH_SHEET(sheet));
        reg->header_canvas = header_canvas;

        gtk_table_attach (GTK_TABLE(widget), header_canvas,
                          0, 1, 0, 1,
                          GTK_FILL | GTK_EXPAND | GTK_SHRINK,
                          GTK_FILL,
                          0, 0);
        
        gtk_table_attach (GTK_TABLE(widget), sheet,
                          0, 1, 1, 2,
                          GTK_FILL | GTK_EXPAND | GTK_SHRINK,
                          GTK_FILL | GTK_EXPAND | GTK_SHRINK,
                          0, 0);

        scrollbar = gtk_vscrollbar_new(GNUCASH_SHEET(sheet)->vadj);
        gtk_table_attach (GTK_TABLE(widget), GTK_WIDGET(scrollbar),
                          1, 2, 0, 2,
                          GTK_FILL,
                          GTK_EXPAND | GTK_SHRINK | GTK_FILL,
                          0, 0);
        reg->vscrollbar = scrollbar;
        gtk_widget_show(scrollbar);
        
        scrollbar = gtk_hscrollbar_new(GNUCASH_SHEET(sheet)->hadj);
        gtk_table_attach (GTK_TABLE(widget), GTK_WIDGET(scrollbar),
                          0, 1, 2, 3,
                          GTK_EXPAND | GTK_SHRINK | GTK_FILL,
                          GTK_FILL,
                          0, 0);
        reg->hscrollbar = scrollbar;
        gtk_widget_show(scrollbar);

        return widget;
}


/*
  Local Variables:
  c-basic-offset: 8
  End:
*/