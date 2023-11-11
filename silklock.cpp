/*
A sample to demo dead lock pitfalls in multi-threads app by Raymond.

Install dependencies:
sudo apt install libgtk2.0-dev

Compile: 
gcc -g3 silklock.c -o silklock `pkg-config --cflags --libs gtk+-2.0 gthread-2.0` -lX11
*/

#include <gtk/gtk.h>
#include <malloc.h>
#include <fcntl.h>
#include <stdlib.h>
#include "boxboss.h"

#define GE_FLAG_THREAD_MALLOC   1
#define GE_FLAG_EXIT            2
#define DATA_BUFFER_LENGTH      2

int * g_databuffer = NULL;
GtkWidget *g_clist = NULL;
GtkWidget *g_editno = NULL;
void * g_lastmalloc = NULL;
volatile int g_flags = 0;
static size_t g_block_size = 1000;
static int g_block_no = 10;
volatile int g_threads = 0; // no of worker threads
volatile int g_malloc_tasks = 0; // var for thread communication
GMutex mutex_list;
int trace_fd = 0; 
CBoxBoss boss;
ChairMan chair;

#define GETASK_IDLE  0
#define GETASK_MALLOC     1
#define GETASK_NULLP_EVEN 2
#define GETASK_NULLP_ODD  3

volatile int g_task = GETASK_IDLE;

static gboolean append_list(char * szMsg)
{
    gchar * text_array[2];
    g_mutex_lock(&mutex_list);

    puts(szMsg);
    text_array[0] = szMsg;
    text_array[1] = NULL;
    gtk_clist_append((GtkCList *)g_clist, text_array);

    g_mutex_unlock(&mutex_list);
}
#define MSG_LINE_MAX 1024
#define GE_WA_GTKBUG // workaround GTK2 bug for list appending
void d4d(const char * format, ...)
{
#ifdef GE_WA_GTKBUG     
    char *msg = new char[MSG_LINE_MAX];
#else
    char msg[MSG_LINE_MAX];
#endif    
    va_list va;
    va_start( va, format );

    vsprintf(msg, format, va);

    append_list(msg);
    //gdk_threads_add_idle(append_list,msg);
}

void do_malloc(size_t nsize, int no, int threadno)
{
    int i = 0;
    for(i=0;i<no;i++)
    {
        g_lastmalloc = malloc(nsize);
		if(g_lastmalloc)
		{
			for(int p=0;p<nsize;p+=4096)
				*((char*)g_lastmalloc+p) =  p;
		}

        d4d("thread %d's %dth malloc(%lld) got return %p", threadno, i, nsize, g_lastmalloc); 
    }
}

/*
(gdb) printf "%llx", nsize
ffffffff80000000
*/

void button_malloc_clicked( gpointer data )
{
    const gchar *entry_text;
    size_t nsize;
    int no;

    entry_text = gtk_entry_get_text(GTK_ENTRY(data));
    if(strstr(entry_text,"0x"))
        nsize=strtoull(entry_text, NULL, 16);
    else
        nsize = atoi(entry_text);

    entry_text = gtk_entry_get_text(GTK_ENTRY(g_editno));
    no = atoi(entry_text);

    do_malloc(nsize, no, 0);

    //
    // set global vars to notify worker threads to do allocation
    if(g_threads>0)
    {
		g_task = GETASK_MALLOC;
        g_malloc_tasks = g_threads; 
        g_block_size =  nsize;
        g_block_no = no;
    }
    //
}

void btn_notify_clicked( gpointer data )
{
    GtkWidget * btn_notify = (GtkWidget *)data;
    bool checked = boss.GetNotify();//gtk_check_button_get_active (btn_notify);
    boss.SetNotify(!checked);

    d4d("notify flag is %s", boss.GetNotify() ? "set" : "cleared");
}

void btn_start_clicked( gpointer data )
{
    boss.StartCalc();

    append_list("calculation is started");
}

void button_free_clicked( gpointer data )
{
    free(g_lastmalloc);    

    d4d("free(%p) returned", g_lastmalloc);
}

void button_overflow_clicked( gpointer data )
{
    const gchar *entry_text;
    int len;
	
    entry_text = gtk_entry_get_text(GTK_ENTRY(g_editno));
    len = atoi(entry_text);

    d4d("going to flood (%p) with length %d", g_lastmalloc, len*2);

    memset(g_lastmalloc,0xbadbad, len*2);    
}

void btn_newtask_clicked( gpointer data )
{
    const gchar *entry_text;
    int threads = 0;


    entry_text = gtk_entry_get_text(GTK_ENTRY(g_editno));
    threads = atoi(entry_text);
    boss.NewTask(1, threads, 10000);
    d4d("new task is planed with %d workers. pls. click start to kick off.", threads);
}

void button_marker_clicked( gpointer data )
{
    const gchar *entry_text;

    entry_text = gtk_entry_get_text(GTK_ENTRY(g_editno));
    
    // trace_write("trace marker %s", entry_text);
}

static void *
thread_func_buggy(void *arg)
{
    int n;
    int tn = (int) (long)arg;
    void * p = NULL;

    d4d("thread %d starts", tn);

    do
    {
        if (g_malloc_tasks>0)
        {
            do_malloc(g_block_size, g_block_no, tn);
            n =__sync_fetch_and_sub(&g_malloc_tasks, 1);
            d4d("thread %d finished its part at %dth", tn, n);
        }
        // asm("pause");
    }while( (g_flags&GE_FLAG_EXIT) == 0 );

    d4d("thread %d exited", tn);

    return NULL;
}
static void crash_handler_a(int sig)
{
    d4d("catcha: received signal %d: ", sig);
    if(sig == SIGSEGV)
    {
       d4d("catcha: SEGMENT FAULT");
#ifdef JUMP_SEGFAULT
       longjmp(env, 1);
#endif
    }
    else if(sig == SIGINT)
       d4d("catcha: INTERRUPTED, by CTRL +C?");
    else
       d4d("catcha: Not translated signal");
}
static void crash_handler_b(int sig)
{
    d4d("catchb: received signal %d: ", sig);
    if(sig == SIGSEGV)
    {
       d4d("catchb: SEGMENT FAULT");
#ifdef JUMP_SEGFAULT
       longjmp(env, 1);
#endif
    }
    else if(sig == SIGINT)
       d4d("catchb: INTERRUPTED, by CTRL +C?");
    else
       d4d("catchb: Not translated signal");
}
static int gs_signals[] = 
{
    SIGSEGV, /* This is the most common crash */
    SIGINT,
    -1
};
static void *
thread_func(void *arg)
{
    int n;
    int tn = (int) (long)arg;
    void * p = NULL;

    d4d("thread %d starts", tn);
	
	signal(SIGSEGV, ((tn%2)==0)?crash_handler_a:crash_handler_b);

    do
    {
		if(g_task==GETASK_MALLOC)
		{
		    if (g_malloc_tasks>0)
		    {
		        n =__sync_fetch_and_sub(&g_malloc_tasks, 1);
		        if(n>0)
		        {
		            do_malloc(g_block_size, g_block_no, tn);

		            d4d("thread %d finished its part at %dth", tn, n);
		        }
		        else
		        {
		            d4d("thread %d saw tasks but failed to fetch 1, cur tasks %d", 
		                tn, g_malloc_tasks);
		        }
		    }
		}
		else if(g_task==GETASK_NULLP_EVEN)
		{
			if((tn%2) == 0)
			{
				char * p = (char*)10;
			    d4d("thread %d is going to crash", tn);
				*p=100;
			}
		}
		else if(g_task==GETASK_NULLP_ODD)
		{
			if((tn%2) == 1)
			{
				char * p = (char*)100;
			    d4d("thread %d is going to crash", tn);
				*p=100;
			}
		}
        // asm("pause");
    }while( (g_flags&GE_FLAG_EXIT) == 0 );

    d4d("thread %d exited", tn);

    return NULL;
}
void button_threads_clicked(GtkWidget *widget, gpointer data )
{
    const gchar *entry_text;
    int tn, numthreads, ret;
    
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) 
    {
        g_flags |= GE_FLAG_EXIT;
        d4d("exit flag is set %x", g_flags);
        return;
    }

    entry_text = gtk_entry_get_text(GTK_ENTRY(data));
    numthreads = atoi(entry_text);

    pthread_t *thr = (pthread_t *)calloc(numthreads, sizeof(pthread_t));
    // clear the exit flag
    g_flags &= ~GE_FLAG_EXIT;
    //

    for (tn = 1; tn <= numthreads; tn++) 
    {
        ret = pthread_create(&thr[tn], NULL, thread_func,
                              (void *) (long)tn);
        if (ret != 0)
        {
           d4d("pthread_create failed with %d", ret);
        }
    }

    free(thr);

    g_threads = numthreads;
    d4d("%d threads created", numthreads);
}


/* User clicked the "Clear List" button. */
void button_clear_clicked( gpointer data )
{
    gtk_clist_clear((GtkCList *)g_clist);
}

void button_nullp_clicked( gpointer data )
{
    g_task = GETASK_NULLP_EVEN;
}
void button_nullp_odd_clicked( gpointer data )
{
    g_task = GETASK_NULLP_ODD;
}

gint main( int    argc,
           gchar *argv[] )
{                                
    gint ret = 0;
    GtkWidget *window = NULL;
    GtkWidget *vbox = NULL, *hbox,*scrolled_window;
    GtkWidget *button_malloc, *button_free, *button_clear, *button_threads, 
        *btn_notify,*btn_start,*btn_newtask,*button_marker,
		*button_overflow,*button_nullp,*button_nullp_odd; 
    GtkWidget * lbl_size,*lbl_no;  
    GtkWidget *edit_size,*edit_no,*edit_threads;

    // XInitThreads();
    boss.Setup(&chair);
    
    // g_thread_init(NULL);
    gdk_threads_init();
    g_mutex_init(&mutex_list);
    //
    //gdk_threads_enter();
    gtk_init(&argc, &argv);

    // create the main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "SilkLock By RAYMOND rev1.0");
    gtk_signal_connect(GTK_OBJECT (window), "delete_event",
                       (GtkSignalFunc) gtk_exit, NULL);
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);
   
    // create a horizonal box at first level
    hbox=gtk_hbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
    gtk_container_add(GTK_CONTAINER(window), hbox);
    gtk_widget_show(hbox);

    // create a vertical box to hold buttons
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_widget_set_usize(vbox, 100, 200);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, TRUE, 0);

    gtk_widget_show(vbox);

    lbl_size = gtk_label_new("Size:");
    gtk_misc_set_alignment (GTK_MISC(lbl_size), 0, 0);
    edit_size = gtk_entry_new_with_max_length (50);
    gtk_entry_set_text(GTK_ENTRY (edit_size), "80");
    lbl_no = gtk_label_new("Number:");
    gtk_misc_set_alignment (GTK_MISC(lbl_no), 0, 0);
    edit_no = gtk_entry_new_with_max_length (50);
    gtk_entry_set_text(GTK_ENTRY (edit_no), "10");
    g_editno = edit_no;

    button_malloc = gtk_button_new_with_label("malloc");
    btn_notify = gtk_check_button_new_with_label("Need Notify");
    //gtk_check_button_set_active(&btn_notify, true); 
    btn_start = gtk_button_new_with_label("Start");
    button_free = gtk_button_new_with_label("free");
    btn_newtask = gtk_button_new_with_label("New Task");
    button_marker = gtk_button_new_with_label("marker");
    button_overflow = gtk_button_new_with_label("overflow");
    button_nullp = gtk_button_new_with_label("nullp even");
    button_nullp_odd = gtk_button_new_with_label("nullp odd");

    edit_threads = gtk_entry_new_with_max_length (50);
    gtk_entry_set_text(GTK_ENTRY (edit_threads), "10");
    button_threads = gtk_check_button_new_with_label("Multi-threads");

    button_clear = gtk_button_new_with_label("Clear List");

    gtk_signal_connect_object(GTK_OBJECT(button_malloc), "clicked",
                              GTK_SIGNAL_FUNC(button_malloc_clicked),
                              (gpointer) edit_size);
    gtk_signal_connect_object(GTK_OBJECT(button_clear), "clicked",
                              GTK_SIGNAL_FUNC(button_clear_clicked),
                              (gpointer) NULL);
    gtk_signal_connect_object(GTK_OBJECT(button_free), "clicked",
                              GTK_SIGNAL_FUNC(button_free_clicked),
                              (gpointer) NULL);
    gtk_signal_connect_object(GTK_OBJECT(btn_newtask), "clicked",
                              GTK_SIGNAL_FUNC(btn_newtask_clicked),
                              (gpointer) NULL);
    gtk_signal_connect_object(GTK_OBJECT(button_overflow), "clicked",
                              GTK_SIGNAL_FUNC(button_overflow_clicked),
                              (gpointer) NULL);
    gtk_signal_connect_object(GTK_OBJECT(button_nullp), "clicked",
                              GTK_SIGNAL_FUNC(button_nullp_clicked),
                              (gpointer) NULL);
    gtk_signal_connect_object(GTK_OBJECT(button_nullp_odd), "clicked",
                              GTK_SIGNAL_FUNC(button_nullp_odd_clicked),
                              (gpointer) NULL);
    gtk_signal_connect_object(GTK_OBJECT(button_marker), "clicked",
                              GTK_SIGNAL_FUNC(button_marker_clicked),
                              (gpointer) NULL);
    gtk_signal_connect_object(GTK_OBJECT(btn_notify), "toggled",
                              GTK_SIGNAL_FUNC(btn_notify_clicked),
                              (gpointer) &btn_notify);
    gtk_signal_connect_object(GTK_OBJECT(btn_start), "clicked",
                              GTK_SIGNAL_FUNC(btn_start_clicked),
                              (gpointer) NULL);
    g_signal_connect(GTK_OBJECT(button_threads), "toggled",
                              G_CALLBACK(button_threads_clicked),
                              (gpointer) edit_threads);

    gtk_box_pack_start(GTK_BOX(vbox), lbl_size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), edit_size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_no, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), edit_no, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_malloc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_notify, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_start, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_free, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_newtask, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_overflow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_nullp, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_nullp_odd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_marker, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), edit_threads, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_threads, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), button_clear, FALSE, FALSE, 0);

    gtk_widget_show(lbl_size);
    gtk_widget_show(edit_size);
    gtk_widget_show(lbl_no);
    gtk_widget_show(edit_no);
    gtk_widget_show(button_malloc);
    gtk_widget_show(btn_notify);
    gtk_widget_show(btn_start);
    gtk_widget_show(button_free);
    gtk_widget_show(btn_newtask);
    gtk_widget_show(button_overflow);
    gtk_widget_show(button_marker);
    gtk_widget_show(edit_threads);
    gtk_widget_show(button_threads);
    gtk_widget_show(button_clear);
    gtk_widget_show(button_nullp);
    gtk_widget_show(button_nullp_odd);

    //
    /* Create a scrolled window to pack the g_clist widget into */
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

    gtk_box_pack_start(GTK_BOX(hbox), scrolled_window, TRUE, TRUE, 0);
    gtk_widget_show (scrolled_window);

    /* Create the g_clist. For this example we use 2 columns */
    g_clist = gtk_clist_new(1);
    gtk_clist_set_column_width (GTK_CLIST(g_clist), 0, 250);

    /* Add the CList widget to the vertical box and show it. */
    gtk_container_add(GTK_CONTAINER(scrolled_window), g_clist);

    gtk_box_pack_start(GTK_BOX(hbox), scrolled_window, TRUE, TRUE, 0);
    gtk_widget_show(g_clist);

    gtk_widget_show(window);

    // init global data buffer
    g_databuffer = (int*)malloc(DATA_BUFFER_LENGTH);

    gtk_main();
    //gdk_threads_leave();

    return ret;
}
