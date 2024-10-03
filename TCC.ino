#include <WiFi.h>
#include <PubSubClient.h>
#include "Adafruit_VL53L0X.h"

// Configurações do Wi-Fi e MQTT
// insira aqui o nome e senha da sua rede
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "broker.emqx.io";

WiFiClient espClient;
PubSubClient client(espClient);

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

const int PINO_TRIG = 4; // AZUL
const int PINO_ECHO = 2; // VERDE
#define MOT_PIN1 18
#define MOT_PIN2 19
const int PINO_BUZZER = 23; // Pino do buzzer

long duracao_us;
float distancia_us;  // Variável global para armazenar a leitura do ultrassom

VL53L0X_RangingMeasurementData_t measure;
float distancia_ls;

// Registro de execução das tarefas
unsigned long ultimoTesteUltrassom = 0;
unsigned long ultimoTesteLaser = 0;
unsigned long ultimoControlarAtuadores = 0;
unsigned long ultimoEnvioMQTT = 0;
unsigned long ultimoEnvioMensagemColisoes = 0;  
unsigned long ultimoTentativaWifi = 0;
unsigned long ultimoTentativaMqtt = 0; 

// Definição de tempo das tarefas (em ms)
const int intervaloUltrassom = 1000;
const int intervaloLaser = 1000;
const int intervaloAtuadores = 1000;
const int intervaloEnvioMQTT = 1000;  // Intervalo para envio de mensagens MQTT
const int intervaloMensagemColisoes = 300000;  // Intervalo para checar o numero de possiveis colisoes (600000 = 10 min)
const int intervaloWifi = 60000;  // Intervalo para tentar reconectar ao Wi-Fi (10 segundos)
const int intervaloMqtt = 60000;  // Intervalo para tentar reconectar ao MQTT

bool wifiConectado = false;  // Status da conexão Wi-Fi


const char* leituras_topic = "tcc-rafael-topico-leituras";
const char* mensagem_colisoes_topic = "tcc-rafael-topico-colisoes";

int numeroColisoesPotenciais = 0;
unsigned long tempoUltimaColisao = 0;

int intensidade = 0; // Intensidade dos motores de vibração

void setup() {
    Serial.begin(9600);

    pinMode(PINO_TRIG, OUTPUT);
    pinMode(PINO_ECHO, INPUT);

    pinMode(MOT_PIN1, OUTPUT);
    pinMode(MOT_PIN2, OUTPUT);
  
    ledcSetup(0, 20000, 8); // Canal 0, 20kHz, 8 bits de resolução
    ledcSetup(1, 20000, 8); // Canal 1, 20kHz, 8 bits de resolução
    ledcAttachPin(MOT_PIN1, 0);
    ledcAttachPin(MOT_PIN2, 1);

    ledcSetup(2, 2000, 8);
    ledcAttachPin(PINO_BUZZER, 2);

    Serial.println("Adafruit VL53L0X test");
    if (!lox.begin()) {
        Serial.println(F("Failed to boot VL53L0X"));
        while (1);
    }
}

void loop() {
    // Tenta conectar ao Wi-Fi sem bloquear a execução das outras funções
    if (!wifiConectado && (millis() - ultimoTentativaWifi >= intervaloWifi)) {
        conectarWifi();  // Tenta conectar ao Wi-Fi
        ultimoTentativaWifi = millis();
    }

    // Tenta reconectar ao MQTT se já estiver conectado ao Wi-Fi
    if (wifiConectado && !client.connected() && (millis() - ultimoTentativaMqtt >= intervaloMqtt)) {
        reconnect();  // Tenta reconectar ao MQTT
        ultimoTentativaMqtt = millis();
    }

    client.loop();  // Mantém o cliente MQTT operando

    // Envia a mensagem com o numero de potenciais colisoes a cada n minutos
    if (millis() - ultimoEnvioMensagemColisoes >= intervaloMensagemColisoes) {
        ultimoEnvioMensagemColisoes = millis();
        tempoUltimaColisao += intervaloMensagemColisoes;

        if (wifiConectado && client.connected()) {
          enviarMensagemColisoes();
        }
    }

    if (millis() - ultimoTesteUltrassom >= intervaloUltrassom) {
        lerUltrassom();
        ultimoTesteUltrassom = millis();
    }

    if (millis() - ultimoTesteLaser >= intervaloLaser) {
        lerLaser();
        ultimoTesteLaser = millis();
    }

    if (millis() - ultimoControlarAtuadores >= intervaloAtuadores) {
        controlarAtuadores(distancia_us, distancia_ls);
        ultimoControlarAtuadores = millis();
    }

    if (wifiConectado && millis() - ultimoEnvioMQTT >= intervaloEnvioMQTT) {
        enviarSensoresMQTT();
        ultimoEnvioMQTT = millis();
    }
}

// Função para tentar conectar ao Wi-Fi (não bloqueante)
void conectarWifi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("Tentando conectar ao Wi-Fi...");
        WiFi.begin(ssid, password);

        unsigned long inicioTentativa = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - inicioTentativa) < 1000) {
            // Espera, mas não bloqueia por mais de 1 segundo
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Conectado ao Wi-Fi");
            Serial.print("Endereço IP: ");
            Serial.println(WiFi.localIP());
            wifiConectado = true;
            client.setServer(mqtt_server, 1883);  // Configura o servidor MQTT após conexão
        } else {
            Serial.println("Falha ao conectar ao Wi-Fi");
            wifiConectado = false;
        }
    }
}

// Função para reconectar ao broker MQTT
void reconnect() {
    Serial.print("Tentando conexão MQTT...");
    if (client.connect("ESP32_Client")) {
        Serial.println("Conectado ao MQTT");
    } else {
        Serial.print("Falhou, rc=");
        Serial.print(client.state());
        Serial.println(" Tentando novamente em 5 segundos");
    }
}

// Função genérica para enviar mensagens via MQTT
void enviarMensagemMQTT(const char* topico, const char* mensagem) {
    if (client.connected()) {
        client.publish(topico, mensagem);
        Serial.print("Mensagem enviada para o tópico ");
        Serial.print(topico);
        Serial.print(": ");
        Serial.println(mensagem);
    } else {
        Serial.println("Erro: Não conectado ao broker MQTT");
    }
}

// Função dedicada para enviar a leitura do sensor de ultrassom via MQTT
void enviarSensoresMQTT() {
    char mensagem[100];
    // snprintf(mensagem, 50, "Distância ultrassom: %.2f cm", distancia_us);
    
    snprintf(mensagem, 150, "Distância ultrassom: %.2f cm\nDistância laser: %.2f cm", distancia_us, (distancia_ls == -1 ? 0 : distancia_ls));

    enviarMensagemMQTT(leituras_topic, mensagem);
}

// Função dedicada para enviar a mensagem com numero de colisoes 
void enviarMensagemColisoes() {
    char mensagem[100];
    // snprintf(mensagem, 50, "Colisões em potencial nos últimos %f ms: %d", ultimoEnvioMensagemColisoes, numeroColisoesPotenciais);
    snprintf(mensagem, 100, "Possíveis colisões nos últimos %d minutos: %d", (tempoUltimaColisao/60000), numeroColisoesPotenciais);
    enviarMensagemMQTT(mensagem_colisoes_topic, mensagem);
    numeroColisoesPotenciais = 0;
    tempoUltimaColisao = 0;
}

float ultimaDistancia_us = 0;

void lerUltrassom() {
    // Leitura do sensor ultrassônico
    digitalWrite(PINO_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PINO_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PINO_TRIG, LOW);

    duracao_us = pulseIn(PINO_ECHO, HIGH);
    distancia_us = (duracao_us * 0.0343) / 2;

    Serial.print("Ultrassom: ");
    if (distancia_us <= 300) {
      Serial.print(distancia_us);
      Serial.println(" cm");
    } else {
      distancia_us = -1;
      Serial.println("Fora de alcance");
    }

    
    
}

void lerLaser() {
    if (distancia_us <= 150 && distancia_us > 0) {
        lox.rangingTest(&measure, false);
        if (measure.RangeStatus != 4) {
            distancia_ls = measure.RangeMilliMeter / 10;
            Serial.print("Laser: ");
            Serial.print(distancia_ls);
            Serial.println(" cm");
        } else {
            Serial.println("Laser: Fora do alcance");
            distancia_ls = -1;
        }
    } else {
        distancia_ls = -1;
        Serial.println("Laser: Desligado");
    }
}

void controlarAtuadores(float dist_us, float dist_ls) {
    if ((dist_us - dist_ls <= 15) && (dist_ls <= 80) && (dist_ls >=0)) {
        ledcWrite(2, 180);
        delay(1000);
        ledcWrite(2, 0);

        numeroColisoesPotenciais += 1;
        
        return;
    }

    if (dist_us <= 300 && dist_us >= 0) {
      ledcWrite(0, 255);
      ledcWrite(1, 255);
    } else {
      ledcWrite(0, 0);
      ledcWrite(1, 0);
    }
}
