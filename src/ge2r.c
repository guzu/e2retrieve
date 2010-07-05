#include <gtk/gtk.h>
#include <getopt.h>

#include "e2retrieve.h"

static GtkWidget *win, *lbl_refdate;
static GtkWidget *notebook;
static GtkWidget *diskmap_da, *diskmap_vp;
static GdkPixmap *diskmap;
static GdkGC *gc_diskmap;

static int nbparts;
static char **parts;

static int pix_unit = 1;

void diskmap_expose (GtkWidget *widget, GdkRectangle *area, gpointer p) {
  g_print("Expose\n");
}

void ge2r_display_refdate(const char* date) {  
  gtk_label_set(GTK_LABEL(lbl_refdate), date);
}

void ge2r_init_scan(void) {  
  struct fs_part *p;

  g_print("ge2r_init_scan\n");

  for(p = ext2_parts; p; p = p->next) {
    off_t nbunit = p->size / (off_t)1024;

    printf("(W,H) = (%lld,%lld)\n", (off_t)640, (off_t)(nbunit/(off_t)640));

    diskmap = gdk_pixmap_new(diskmap_da->window,
			     (gint)640,
			     (gint)(nbunit/(off_t)640),
			     -1);

    gtk_drawing_area_size(GTK_DRAWING_AREA(diskmap_da), 640, nbunit/640);

    if(gc_diskmap == NULL) {
      GdkColor white;
      
      gc_diskmap = gdk_gc_new(diskmap);
      gdk_color_white(gdk_colormap_get_system(), &white);
      
      gdk_gc_set_foreground(gc_diskmap, &white);
    }

    gdk_draw_rectangle(diskmap, gc_diskmap, TRUE,
		       0, 0,
		       100, 100);
  }
}

void diskmap_redraw (GtkWidget *widget, GdkRectangle *area, gpointer p) {
  g_print("Draw (%d, %d) (%d, %d)\n", area->x, area->y, area->width, area->height);
}

void start_scan(GtkWidget *widget, GdkRectangle *area, gpointer p) {
  g_print("start_scan\n");
  do_it(nbparts, parts);
}

void build_ihm(void) {
  GtkWidget *hbox, *vscroll, *vbox_win; 
  GtkWidget *bt_start;
  GtkAdjustment *adj_v;

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  vbox_win = gtk_vbox_new(0, 0);
  gtk_container_add(GTK_CONTAINER(win), vbox_win);

  bt_start = gtk_button_new_with_label("Start");
  gtk_signal_connect(GTK_OBJECT(bt_start), "button_press_event", GTK_SIGNAL_FUNC (start_scan), NULL);
  gtk_box_pack_start(GTK_BOX(vbox_win), bt_start, FALSE, FALSE, 0);

  notebook = gtk_notebook_new();
  gtk_box_pack_start(GTK_BOX(vbox_win), notebook, TRUE, TRUE, 0);

  lbl_refdate = gtk_label_new("???");
  gtk_box_pack_end(GTK_BOX(vbox_win), lbl_refdate, FALSE, FALSE, 0);

  hbox = gtk_hbox_new(0, 0);

  adj_v = (GtkAdjustment*) gtk_adjustment_new( 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 );

  diskmap_vp = gtk_viewport_new(adj_v, NULL);
  gtk_box_pack_start(GTK_BOX(hbox), diskmap_vp, TRUE, TRUE, 0);

  diskmap_da = gtk_drawing_area_new();
  gtk_drawing_area_size(GTK_DRAWING_AREA(diskmap_da), 640, 480);
  gtk_signal_connect(GTK_OBJECT(diskmap_da), "expose_event", GTK_SIGNAL_FUNC (diskmap_expose), NULL);
  gtk_signal_connect(GTK_OBJECT(diskmap_da), "draw",         GTK_SIGNAL_FUNC (diskmap_redraw), NULL);
  gtk_container_add(GTK_CONTAINER(diskmap_vp), diskmap_da);

  // Créé la scrollbar verticale
  vscroll = gtk_vscrollbar_new(adj_v);
  gtk_box_pack_end(GTK_BOX(hbox), vscroll, FALSE, FALSE, 0);
  gtk_widget_show(vscroll);
  
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), hbox, gtk_label_new("Scanning..."));

  gtk_widget_show_all(win);
}

int main(int argc, char *argv[]) {
  int r, w;

  gtk_init(&argc, &argv);

  build_ihm();
  ihm_funcs.init_scan       = ge2r_init_scan;
  ihm_funcs.display_refdate = ge2r_display_refdate;

  parse_cmdline(argc, argv);

  /* put part files at the beginning of argv */
  w = 0;
  for(r = optind; r < argc; r++)
    argv[w++] = argv[r];
  nbparts = w;
  parts = argv;

  gtk_main();

  return 0;
}
