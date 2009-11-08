import dbus.glib
import gobject
import telepathy

from telepathy._generated.Client_Observer import ClientObserver
from telepathy.server.properties import DBusProperties
from telepathy.interfaces import CLIENT, \
                                 CLIENT_OBSERVER, \
                                 CHANNEL

class ExampleObserver(ClientObserver, DBusProperties):
    properties = {
        CLIENT: {
            'Interfaces': [ CLIENT_OBSERVER ],
        },
        CLIENT_OBSERVER: {
            'ObserverChannelFilter': dbus.Array([
                    dbus.Dictionary({
                    }, signature='sv')
                ], signature='a{sv}')
        },
    }

    def __init__(self, client_name):
        bus_name = '.'.join ([CLIENT, client_name])
        object_path = '/' + bus_name.replace('.', '/')

        bus_name = dbus.service.BusName(bus_name, bus=dbus.SessionBus())
        ClientObserver.__init__(self, bus_name, object_path)

    def GetAll(self, interface):
        print "GetAll", interface
        if interface in self.properties:
            return self.properties[interface]
        else:
            return {}

    def Get(self, interface, property):
        print "Get", interface, property
        if interface in self.properties and \
           property in self.properties[interface]:
            return self.properties[interface][property]
        else:
            return 0

    def ObserveChannels(self, account, connection, channels, dispatch_operation,
                        requests_satisfied, observer_info):

        print "Incoming channels on %s:" % (connection)
        for object, props in channels:
            print " - %s :: %s" % (props[CHANNEL + '.ChannelType'],
                                   props[CHANNEL + '.TargetID'])

def start():
    ExampleObserver("ExampleObserver")
    return False

if __name__ == '__main__':
    gobject.timeout_add(0, start)
    loop = gobject.MainLoop()
    loop.run()
