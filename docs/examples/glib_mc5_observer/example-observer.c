#include <telepathy-glib/svc-client.h>

#include "example-observer.h"

static void client_iface_init (gpointer, gpointer);
static void observer_iface_init (gpointer, gpointer);

// #define EXAMPLE_OBSERVER_GET_PRIVATE(obj)	(G_TYPE_INSTANCE_GET_PRIVATE ((obj), EXAMPLE_TYPE_OBSERVER, ExampleObserverPrivate))

G_DEFINE_TYPE_WITH_CODE (ExampleObserver, example_observer, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CLIENT, client_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CLIENT_OBSERVER, observer_iface_init);
    );

// typedef struct _ExampleObserverPrivate ExampleObserverPrivate;
// struct _ExampleObserverPrivate
// {
// };

static void
example_observer_observe_channels (TpSvcClientObserver   *self,
                                   const char            *account,
                                   const char            *connection,
                                   const GPtrArray       *channels,
                                   const char            *dispatch_op,
                                   const GPtrArray       *requests_satisfied,
                                   GHashTable            *observer_info,
                                   DBusGMethodInvocation *context)
{
  g_print (" > example_observer_observe_channels\n");
  g_print ("     account    = %s\n", account);
  g_print ("     connection = %s\n", connection);
  g_print ("     dispatchop = %s\n", dispatch_op);

  tp_svc_client_observer_return_from_observe_channels (context);
}

static void
example_observer_class_init (ExampleObserverClass *class)
{
}

static void
example_observer_init (ExampleObserver *self)
{
}

static void
client_iface_init (gpointer g_iface, gpointer iface_data)
{
  /* no methods */
}

static void
observer_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcClientObserverClass *klass = (TpSvcClientObserverClass *) g_iface;

#define IMPLEMENT(x) tp_svc_client_observer_implement_##x (klass, \
    example_observer_##x)
  IMPLEMENT (observe_channels);
#undef IMPLEMENT
}

ExampleObserver *
example_observer_new (void)
{
  return g_object_new (TYPE_EXAMPLE_OBSERVER, NULL);
}
