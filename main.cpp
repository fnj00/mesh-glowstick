#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>
#include <iostream>
#include <iterator>

#include "Arduino.h"

WiFiClass WiFi;
ESPClass ESP;

#include "painlessMesh.h"
#include "painlessMeshConnection.h"
#include "plugin/performance.hpp"

#include "ArtNet.h"

// set these to whatever you like - don't use your home info - this is a separate private network
#define   MESH_PREFIX     "LEDMesh01"
#define   MESH_PASSWORD   "foofoofoo"
#define   MESH_PORT       5555

#define   NUM_LEDS        14

int display_mode = 1;       // this is the current animation type
uint16_t display_step = 0;  // this is a count of the steps in the current animation
bool amController = true;  // flag to designat that this CPU is the current controller
#define MODE6CLICKSPERCOLOR 10   // the number of 50us counts to hold one color
#define MODE6NUMLOOPS 8         // the number of times to change the color

unsigned long ul_PreviousMillis = 0UL;   // using millis rollver code from http://playground.arduino.cc/Code/TimingRollover
unsigned long ul_Interval = 20UL;        // this is delay between animation steps ms

painlessmesh::logger::LogClass Log;

    // check the timer and do one animation step if needed
    void stepAnimation() {
      // if the animation delay time has past, run another animation step
      unsigned long ul_CurrentMillis = millis();
      if (ul_CurrentMillis - ul_PreviousMillis > ul_Interval) {
        ul_PreviousMillis = millis();
        display_step++;
        // a debug led to show which is the current controller
        //if (amController) leds[0] = CRGB(64, 64, 64);
      } // timing conditional ul_Interval
    } // stepAnimation

#undef F
#include <boost/date_time.hpp>
#include <boost/program_options.hpp>
#define F(string_literal) string_literal
namespace po = boost::program_options;

#include <iostream>
#include <iterator>
#include <limits>
#include <random>

#define OTA_PART_SIZE 1024
#include "ota.hpp"

using boost::asio::ip::udp;

template <class T>
// bool contains(T &v, T::value_type const value) {
bool contains(T& v, std::string const value) {
  return std::find(v.begin(), v.end(), value) != v.end();
}

std::string timeToString() {
  boost::posix_time::ptime timeLocal =
      boost::posix_time::second_clock::local_time();
  return to_iso_extended_string(timeLocal);
}

// Will be used to obtain a seed for the random number engine
static std::random_device rd;
static std::mt19937 gen(rd());

uint32_t runif(uint32_t from, uint32_t to) {
  std::uniform_int_distribution<uint32_t> distribution(from, to);
  return distribution(gen);
}

int main(int ac, char* av[]) {
  using namespace painlessmesh;
  try {
    size_t port = 5555;
    std::string ip = "";
    std::vector<std::string> logLevel;
    size_t nodeId = runif(0, std::numeric_limits<uint32_t>::max());
    std::string otaDir;
    double performance = 2.0;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "Produce this help message")(
        "nodeid,n", po::value<size_t>(&nodeId),
        "Set nodeID, otherwise set to a random value")(
        "port,p", po::value<size_t>(&port), "The mesh port (default is 5555)")(
        "server,s",
        "Listen to incoming node connections. This is the default, unless "
        "--client "
        "is specified. Specify both if you want to both listen for incoming "
        "connections and try to connect to a specific node.")(
        "client,c", po::value<std::string>(&ip),
        "Connect to another node as a client. You need to provide the ip "
        "address of the node.")(
        "log,l", po::value<std::vector<std::string>>(&logLevel),
        "Only log given events to the console. By default all events are "
        "logged, this allows you to filter which ones to log. Events currently "
        "logged are: receive, connect, disconnect, change, offset and delay. "
        "This option can be specified multiple times to log multiple types of "
        "events.")("ota-dir,d", po::value<std::string>(&otaDir),
                   "Watch given folder for new firmware files.")(
        "performance", po::value<double>(&performance)->implicit_value(2.0),
        "Enable performance monitoring. Optional value is frequency (per "
        "second) to send performance monitoring packages. Default is every 2 "
        "seconds.");

    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 0;
    }

    Scheduler scheduler;
    boost::asio::io_service io_service;

    udp::socket socket(io_service, udp::endpoint(udp::v4(), 6454));

    for (;;)
    {
      boost::array<char, 1> recv_buf;
      udp::endpoint remote_endpoint;
      boost::system::error_code error;
      socket.receive_from(boost::asio::buffer(recv_buf),
          remote_endpoint, 0, error);

      if (error && error != boost::asio::error::message_size)
        throw boost::system::system_error(error);

//      std::string message = make_daytime_string();

      boost::system::error_code ignored_error;
//      socket.send_to(boost::asio::buffer(message),
//          remote_endpoint, 0, ignored_error);
    }

    painlessMesh mesh;

    Log.setLogLevel(ERROR);
    mesh.init(&scheduler, nodeId, port);
//    mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
    std::shared_ptr<AsyncServer> pServer;

    if (vm.count("server") || !vm.count("client")) {
      pServer = std::make_shared<AsyncServer>(io_service, port);
      painlessmesh::tcp::initServer<MeshConnection, painlessMesh>(*pServer,
                                                                  mesh);
    }

    if (vm.count("client")) {
      auto pClient = new AsyncClient(io_service);
      painlessmesh::tcp::connect<MeshConnection, painlessMesh>(
          (*pClient), boost::asio::ip::address::from_string(ip), port, mesh);
    }

    if (logLevel.size() == 0 || contains(logLevel, "receive")) {
      mesh.onReceive([&mesh](uint32_t nodeId, std::string& msg) {
        std::cout << "{\"event\":\"receive\",\"nodeTime\":"
                  << mesh.getNodeTime() << ",\"time\":\"" << timeToString()
                  << "\""
                  << ",\"nodeId\":" << nodeId << ",\"msg\":\"" << msg << "\"}"
                  << std::endl;
      });
    }
    if (logLevel.size() == 0 || contains(logLevel, "connect")) {
      mesh.onNewConnection([&mesh](uint32_t nodeId) {
        std::cout << "{\"event\":\"connect\",\"nodeTime\":"
                  << mesh.getNodeTime() << ",\"time\":\"" << timeToString()
                  << "\""
                  << ",\"nodeId\":" << nodeId
                  << ", \"layout\":" << mesh.asNodeTree().toString() << "}"
                  << std::endl;
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "disconnect")) {
      mesh.onDroppedConnection([&mesh](uint32_t nodeId) {
        std::cout << "{\"event\":\"disconnect\",\"nodeTime\":"
                  << mesh.getNodeTime() << ",\"time\":\"" << timeToString()
                  << "\""
                  << ",\"nodeId\":" << nodeId
                  << ", \"layout\":" << mesh.asNodeTree().toString() << "}"
                  << std::endl;
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "change")) {
      mesh.onChangedConnections([&mesh]() {
        std::cout << "{\"event\":\"change\",\"nodeTime\":" << mesh.getNodeTime()
                  << ",\"time\":\"" << timeToString() << "\""
                  << ", \"layout\":" << mesh.asNodeTree().toString() << "}"
                  << std::endl;
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "offset")) {
      mesh.onNodeTimeAdjusted([&mesh](int32_t offset) {
        std::cout << "{\"event\":\"offset\",\"nodeTime\":" << mesh.getNodeTime()
                  << ",\"time\":\"" << timeToString() << "\""
                  << ",\"offset\":" << offset << "}" << std::endl;
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "delay")) {
      mesh.onNodeDelayReceived([&mesh](uint32_t nodeId, int32_t delay) {
        std::cout << "{\"event\":\"delay\",\"nodeTime\":" << mesh.getNodeTime()
                  << ",\"time\":\"" << timeToString() << "\""
                  << ",\"nodeId\":" << nodeId << ",\"delay\":" << delay << "}"
                  << std::endl;
      });
    }

    if (vm.count("performance")) {
      plugin::performance::begin(mesh, performance);
    }

    if (vm.count("ota-dir")) {
      using namespace painlessmesh::plugin;
      // We probably want to temporary store the file
      // md5 -> data
      auto files = std::make_shared<std::map<std::string, std::string>>();
      // Setup task that monitors the folder for changes
      auto task =
          mesh.addTask(TASK_SECOND, TASK_FOREVER, [files, &mesh, otaDir]() {
            // TODO: Scan for change
            boost::filesystem::path p(otaDir);
            boost::filesystem::directory_iterator end_itr;
            for (boost::filesystem::directory_iterator itr(p); itr != end_itr;
                 ++itr) {
              if (!boost::filesystem::is_regular_file(itr->path())) {
                continue;
              }
              auto stat = addFile(files, itr->path(), TASK_SECOND);
              if (stat.newFile) {
                // When change, announce it, load it into files
                ota::Announce announce;
                announce.md5 = stat.md5;
                announce.role = stat.role;
                announce.hardware = stat.hw;
                announce.noPart =
                    ceil(((float)files->operator[](stat.md5).length()) /
                         OTA_PART_SIZE);
                announce.from = mesh.getNodeId();

                auto announceTask = mesh.addTask(
                    TASK_MINUTE, 60,
                    [&mesh, announce]() { mesh.sendPackage(&announce); });
                // after anounce, remove file from memory
                announceTask->setOnDisable(
                    [files, md5 = stat.md5]() { files->erase(md5); });
              }
            }
          });
      // Setup reply to data requests
      mesh.onPackage(11, [files, &mesh](protocol::Variant variant) {
        auto pkg = variant.to<ota::DataRequest>();
        // cut up the data and send it
        if (files->count(pkg.md5)) {
          auto reply =
              ota::Data::replyTo(pkg,
                                 files->operator[](pkg.md5).substr(
                                     OTA_PART_SIZE * pkg.partNo, OTA_PART_SIZE),
                                 pkg.partNo);
          mesh.sendPackage(&reply);
        } else {
          Log(ERROR, "File not found");
        }
        return true;
      });
    }
    while (true) {
      usleep(1000);  // Tweak this for acceptable cpu usage
      mesh.update();
      stepAnimation();
      // if the current animation is done, start the next one - only the controller does this, and it messages the rest
      if (amController) {
        bool display_mode_changed = false;
        switch (display_mode) {
          case 1: // run the single color up the bar one LED at a time
            if (display_step > NUM_LEDS) {
              display_mode_changed = true;
              display_mode = 2;  // show all the animations
              //display_mode = 4;  // skip to the holiday only ones
              ul_Interval = 50UL;
            }
            break;

          case 2: // rainbow colors
            if (display_step > 255) {
              display_mode_changed = true;
              display_mode = 3;
              ul_Interval = 2UL;
            }
            break;

          case 3: // go from dim to full on one color for all LEDs a few times
            if (display_step > 1024) {
              display_mode_changed = true;
              display_mode = 4;
              ul_Interval = 50UL;
            }
            break;

          case 4: // alternate red and green LEDs
            if (display_step > (NUM_LEDS*4)) {
              display_mode_changed = true;
              display_mode = 1;
              ul_Interval = 50UL;
            }
            break;

         } // display_mode switch

         // if the display mode changed, do some common tasks and set some common vars
         if (display_mode_changed) {
           std::string msg = std::to_string(display_mode);
           mesh.sendBroadcast(msg);
           display_step = 0;
           ul_PreviousMillis = 0UL; // make sure animation triggers on first step
         }
      } // amController
      io_service.poll();
    }
  } catch (std::exception& e) {
    std::cerr << "error: " << e.what() << std::endl;
    ;
    return 1;
  } catch (...) {
    std::cerr << "Exception of unknown type!" << std::endl;
    ;
  }

  return 0;
}
