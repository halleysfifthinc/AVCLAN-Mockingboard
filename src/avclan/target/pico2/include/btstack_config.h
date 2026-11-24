#pragma once

// BTstack features that can be enabled
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#if AVCLAN_BLUETOOTH_LOG
  #define ENABLE_LOG_INFO
#endif

#ifdef ENABLE_BLE
  #define ENABLE_LE_PERIPHERAL
  #define ENABLE_LE_SECURE_CONNECTIONS
  #define ENABLE_LE_ENHANCED_CONNECTION_COMPLETE_EVENT
#endif

#ifdef ENABLE_CLASSIC
  #define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
  #define ENABLE_GOEP_L2CAP
#endif

#if defined(ENABLE_CLASSIC) && defined(ENABLE_BLE)
  #define ENABLE_CROSS_TRANSPORT_KEY_DERIVATION
#endif

// BTstack configuration. buffers, sizes, ...
#define HCI_OUTGOING_PRE_BUFFER_SIZE              4
#define HCI_ACL_PAYLOAD_SIZE                      (1691 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT              4
// AVDTP, AVRCP (controller + target) and L2CAP hold two phones, which overlap
// while one takes over from the other
#define MAX_NR_AVDTP_CONNECTIONS                  2
#define MAX_NR_AVDTP_STREAM_ENDPOINTS             1
#define MAX_NR_AVRCP_CONNECTIONS                  4
#define MAX_NR_BNEP_CHANNELS                      0
#define MAX_NR_BNEP_SERVICES                      0
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 2
#define MAX_NR_GATT_CLIENTS                       0
#define MAX_NR_HCI_CONNECTIONS                    2
#define MAX_NR_HID_HOST_CONNECTIONS               0
#define MAX_NR_HFP_CONNECTIONS                    0
#define MAX_NR_L2CAP_CHANNELS                     8
#define MAX_NR_L2CAP_SERVICES                     3
#define MAX_NR_RFCOMM_CHANNELS                    0
#define MAX_NR_RFCOMM_MULTIPLEXERS                0
#define MAX_NR_RFCOMM_SERVICES                    0
#define MAX_NR_SERVICE_RECORD_ITEMS               4
#define MAX_NR_SM_LOOKUP_ENTRIES                  0
#define MAX_NR_WHITELIST_ENTRIES                  0
// Only the TLV LE device DB is used; the memory-backed one just needs this to
// compile.
#define MAX_NR_LE_DEVICE_DB_ENTRIES 1

// Limit number of ACL/SCO Buffer to use by stack to avoid cyw43 shared bus
// overrun
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

// Enable and configure HCI Controller to Host Flow Control to avoid cyw43
// shared bus overrun
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

// Link Key DB and LE Device DB using TLV on top of Flash Sector interface
#define NVM_NUM_DEVICE_DB_ENTRIES 8
#define NVM_NUM_LINK_KEYS         8

// No GATT server; att_db_util.c needs a nonzero size to compile without malloc.
#define MAX_ATT_DB_SIZE 1

// BTstack HAL configuration
#define HAVE_EMBEDDED_TIME_MS

// map btstack_assert onto Pico SDK assert()
#define HAVE_ASSERT

// Some USB dongles take longer to respond to HCI reset (e.g. BCM20702A).
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#define HAVE_BTSTACK_STDIN

// To get the audio demos working even with HCI dump at 115200, this truncates
// long ACL packets
// #define HCI_DUMP_STDOUT_MAX_SIZE_ACL 100
