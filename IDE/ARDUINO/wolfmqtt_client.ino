#include <Ethernet.h>
#include <wolfMQTT.h>
#include <wolfssl.h>
#include <wolfssl/ssl.h>
#include <wolfmqtt/mqtt_client.h>
#include <examples/mqttnet.h>

/* Configuration */
#define DEFAULT_CMD_TIMEOUT_MS  1000
#define DEFAULT_CON_TIMEOUT_MS  5000
#define DEFAULT_MQTT_QOS        MQTT_QOS_0
#define DEFAULT_KEEP_ALIVE_SEC  60
#define DEFAULT_CLIENT_ID       "WolfMQTTClient"
#define WOLFMQTT_TOPIC_NAME     "wolfMQTT/example/"

#define MAX_BUFFER_SIZE         1024
#define TEST_MESSAGE            "test"
#define TEST_TOPIC_COUNT        2

EthernetClient client;

int EthernetSend(WOLFSSL* ssl, char* msg, int sz, void* ctx);
int EthernetReceive(WOLFSSL* ssl, char* reply, int sz, void* ctx);
static int mqttclient_tls_cb(MqttClient* cli);
static int mqttclient_tls_verify_cb(int preverify, WOLFSSL_X509_STORE_CTX* store);

WOLFSSL_METHOD* method = 0;
WOLFSSL_CTX* ctx       = 0;
WOLFSSL* ssl           = 0;
word16 port            = 0;
const char* host       = "iot.eclipse.org";
static int mStopRead   = 0;
const char* mTlsFile   = NULL;

void setup() {
  Serial.begin(9600);

  method = wolfTLSv1_2_client_method();
  if (method == NULL) {
    Serial.println("unable to get method");
    return;
  }
  ctx = wolfSSL_CTX_new(method);
  if (ctx == NULL) {
    Serial.println("unable to get ctx");
    return;
  }
  // initialize wolfSSL using callback functions
  wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
  wolfSSL_SetIOSend(ctx, EthernetSend);
  wolfSSL_SetIORecv(ctx, EthernetReceive);

  return;
}

int EthernetSend(WOLFSSL* ssl, char* msg, int sz, void* ctx) {
  int sent = 0;

  sent = client.write((byte*)msg, sz);

  return sent;
}

int EthernetReceive(WOLFSSL* ssl, char* reply, int sz, void* ctx) {
  int recvd = 0;

  while (client.available() > 0) { // && recvd < sz) {
    reply[recvd] = client.read();
    recvd++;
  }

  return recvd;
}

static int mqttclient_tls_verify_cb(int preverify, WOLFSSL_X509_STORE_CTX* store)
{
  char buffer[WOLFSSL_MAX_ERROR_SZ];

  printf("MQTT TLS Verify Callback: PreVerify %d, Error %d (%s)\n", preverify,
         store->error, wolfSSL_ERR_error_string(store->error, buffer));
  printf("  Subject's domain name is %s\n", store->domain);

  /* Allowing to continue */
  /* Should check certificate and return 0 if not okay */
  printf("  Allowing cert anyways\n");

  return 1;
}

static int mqttclient_tls_cb(MqttClient* cli)
{
  int rc = SSL_FAILURE;
  (void)cli; /* Supress un-used argument */

  wolfSSL_CTX_set_verify(cli->tls.ctx, SSL_VERIFY_PEER, mqttclient_tls_verify_cb);

  if (mTlsFile) {
    /* Load CA certificate file */
    rc = wolfSSL_CTX_load_verify_locations(cli->tls.ctx, mTlsFile, 0);
  }
  else {
    rc = SSL_SUCCESS;
  }

  printf("MQTT TLS Setup (%d)\n", rc);

  return rc;
}

#define MAX_PACKET_ID   ((1 << 16) - 1)                                         
static int mPacketIdLast;
static word16 mqttclient_get_packetid(void)
{
  mPacketIdLast = (mPacketIdLast >= MAX_PACKET_ID) ?
                  1 : mPacketIdLast + 1;
  return (word16)mPacketIdLast;
}

void loop() {
  int rc;
  MqttClient cli;
  MqttNet net;
  int use_tls = 0;
  MqttQoS qos = DEFAULT_MQTT_QOS;
  byte clean_session = 1;
  word16 keep_alive_sec = 60;
  const char* client_id = "wolfMQTTClient";
  int enable_lwt = 0;
  const char* username = NULL;
  const char* password = NULL;
  byte tx_buf[MAX_BUFFER_SIZE];
  byte rx_buf[MAX_BUFFER_SIZE];
  MqttMsgCb msg_cb;

  Serial.print("MQTT Client: QoS ");
  Serial.println(qos);
  rc = MqttClientNet_Init(&net);
  Serial.print("MQTT Net Init: ");
  Serial.print(MqttClient_ReturnCodeToString(rc));
  Serial.print(" ");
  Serial.println(rc);

  rc = MqttClient_NetConnect(&cli, host, port,
                             5000, use_tls, mqttclient_tls_cb);
  if (!rc) {
    Serial.println("Could not connect");
    return; 
  }
  Serial.println("Got past connect");

  rc = MqttClient_Init(&cli, &net, msg_cb, tx_buf, MAX_BUFFER_SIZE,
                       rx_buf, MAX_BUFFER_SIZE, DEFAULT_CMD_TIMEOUT_MS);
  Serial.print("MQTT Init: ");
  Serial.print(MqttClient_ReturnCodeToString(rc));
  Serial.print(" ");
  Serial.println(rc);

  /* Connect to broker */
  rc = MqttClient_NetConnect(&cli, host, port,
                             DEFAULT_CON_TIMEOUT_MS, use_tls, mqttclient_tls_cb);
  Serial.print("MQTT Socket Connect: ");
  Serial.print(MqttClient_ReturnCodeToString(rc));
  Serial.print(" ");
  Serial.println(rc);

  if (rc == 0) {
    /* Define connect parameters */
    MqttConnect connect;
    MqttMessage lwt_msg;
    memset(&connect, 0, sizeof(MqttConnect));
    connect.keep_alive_sec = keep_alive_sec;
    connect.clean_session = clean_session;
    connect.client_id = client_id;
    /* Last will and testament sent by broker to subscribers
        of topic when broker connection is lost */
    memset(&lwt_msg, 0, sizeof(lwt_msg));
    connect.lwt_msg = &lwt_msg;
    connect.enable_lwt = enable_lwt;
    if (enable_lwt) {
      /* Send client id in LWT payload */
      lwt_msg.qos = qos;
      lwt_msg.retain = 0;
      lwt_msg.topic_name = WOLFMQTT_TOPIC_NAME"lwttopic";
      lwt_msg.buffer = (byte*)DEFAULT_CLIENT_ID;
      lwt_msg.total_len = (word16)strlen(DEFAULT_CLIENT_ID);
    }
    /* Optional authentication */
    connect.username = username;
    connect.password = password;

    /* Send Connect and wait for Connect Ack */
    rc = MqttClient_Connect(&cli, &connect);
    printf("MQTT Connect: %s (%d)\n",
           MqttClient_ReturnCodeToString(rc), rc);
    if (rc == MQTT_CODE_SUCCESS) {
      MqttSubscribe subscribe;
      MqttUnsubscribe unsubscribe;
      MqttTopic topics[TEST_TOPIC_COUNT], *topic;
      MqttPublish publish;
      int i;

      /* Build list of topics */
      topics[0].topic_filter = WOLFMQTT_TOPIC_NAME"subtopic1";
      topics[0].qos = qos;
      topics[1].topic_filter = WOLFMQTT_TOPIC_NAME"subtopic2";
      topics[1].qos = qos;

      /* Validate Connect Ack info */
      Serial.print("MQTT Connect Ack: Return Code ");
      Serial.print(connect.ack.return_code);
      Serial.print(", Session Present ");
      Serial.println((connect.ack.flags & MQTT_CONNECT_ACK_FLAG_SESSION_PRESENT) ? 1 : 0);

      /* Send Ping */
      rc = MqttClient_Ping(&cli);
      Serial.print("MQTT Ping: ");
      Serial.print(MqttClient_ReturnCodeToString(rc));
      Serial.print(" ");
      Serial.println(rc);

      /* Subscribe Topic */
      memset(&subscribe, 0, sizeof(MqttSubscribe));
      subscribe.packet_id = mqttclient_get_packetid();
      subscribe.topic_count = TEST_TOPIC_COUNT;
      subscribe.topics = topics;
      rc = MqttClient_Subscribe(&cli, &subscribe);
      Serial.print("MQTT Subscribe: ");
      Serial.print(MqttClient_ReturnCodeToString(rc));
      Serial.print(" ");
      Serial.println(rc);
      for (i = 0; i < subscribe.topic_count; i++) {
        topic = &subscribe.topics[i];
        Serial.print("  Topic ");
        Serial.print(topic->topic_filter);
        Serial.print(" Qos ");
        Serial.print(topic->qos);
        Serial.print(" Return Code ");
        Serial.println(topic->return_code);
      }

      /* Publish Topic */
      memset(&publish, 0, sizeof(MqttPublish));
      publish.retain = 0;
      publish.qos = qos;
      publish.duplicate = 0;
      publish.topic_name = WOLFMQTT_TOPIC_NAME"pubtopic";
      publish.packet_id = mqttclient_get_packetid();
      publish.buffer = (byte*)TEST_MESSAGE;
      publish.total_len = (word16)strlen(TEST_MESSAGE);
      rc = MqttClient_Publish(&cli, &publish);
      Serial.print("MQTT Publish: Topic ");
      Serial.print(publish.topic_name);
      Serial.print(", ");
      Serial.print(MqttClient_ReturnCodeToString(rc));
      Serial.print(", ");
      Serial.println(rc);

      /* Read Loop */
      Serial.println("MQTT Waiting for message...");
      while (mStopRead == 0) {
        /* Try and read packet */
        rc = MqttClient_WaitMessage(&cli, DEFAULT_CMD_TIMEOUT_MS);
        Serial.print("..");
        if (rc != MQTT_CODE_SUCCESS && rc != MQTT_CODE_ERROR_TIMEOUT) {
          /* There was an error */
          Serial.print("MQTT Message Wait: ");
          Serial.print(MqttClient_ReturnCodeToString(rc));
          Serial.print(" ");
          Serial.println(rc);
          break;
        }
      }

      /* Unsubscribe Topics */
      memset(&unsubscribe, 0, sizeof(MqttUnsubscribe));
      unsubscribe.packet_id = mqttclient_get_packetid();
      unsubscribe.topic_count = TEST_TOPIC_COUNT;
      unsubscribe.topics = topics;
      rc = MqttClient_Unsubscribe(&cli, &unsubscribe);
      Serial.print("MQTT Unsubscribe: ");
      Serial.print(MqttClient_ReturnCodeToString(rc));
      Serial.print(" ");
      Serial.println(rc);

      /* Disconnect */
      rc = MqttClient_Disconnect(&cli);
      Serial.print("MQTT Disconnect: ");
      Serial.print(MqttClient_ReturnCodeToString(rc));
      Serial.print(" ");
      Serial.println(rc);
    }

    rc = MqttClient_NetDisconnect(&cli);
    Serial.print("MQTT Socket Disconnect: ");
    Serial.print(MqttClient_ReturnCodeToString(rc));
    Serial.print(" ");
    Serial.println(rc);
  }
}

