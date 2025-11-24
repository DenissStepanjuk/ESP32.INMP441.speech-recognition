/**
  Подготовил: Степанюк Денис Борисович
**/

#include <driver/i2s.h>
// Библиотека для работы с wifi подключением (standard library).
#include <WiFi.h>  
// Библиотека для создания и запуска веб-сервера, обработки HTTP-запросов, формирования и отправки HTTP-ответов клиенту.
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
// Библиотека позволяет устанавливать постоянное соединение между сервером и клиентом, что делает возможным двустороннюю передачу данных в реальном времени.
#include <WebSocketsServer.h> 
// Библиотека используется для работы с JSON (JavaScript Object Notation) данными.
#include <ArduinoJson.h>
#include <Arduino.h>
// Библиотека предоставляет функции для преобразования изображений между различными форматами.
#include "img_converters.h"
// Документ хранящий код HTML странички.
#include "HomePage.h"


// Данные WiFi сети.
const char* ssid = "WiFi_name";
const char* password = "WiFi_password";

// Экземпляр вебсервера ссылается на 80 порт.
AsyncWebServer server(80);
// Экземпляр WebSocket-сервера ссылается на 81 порт.
WebSocketsServer webSocket = WebSocketsServer(81);


// Переменная отображает состояние кнопки отвечающей за скачивание теплового снимка.
bool dataset = false;

/** В документе реализлвана функция webSocketEvent для обработки данных 
полученых через соеденение сокетов. **/
#include "socketConnection.h"


// --- Настройка пинов I2S ---
#define I2S_WS   16         // LRCL (word select, left/right clock)
#define I2S_SD   8          // DOUT (data)
#define I2S_SCK  9          // BCLK (bit clock)
#define I2S_PORT I2S_NUM_0  // номер I2S интерфейса (I2S_NUM_0 / I2S_NUM_1) 

// --- Настройки записи ---
#define SAMPLE_RATE     16000           // Частота дискретизации
#define BITS_PER_SAMPLE 16              // Глубина квантования (бит)
#define CHANNELS        1               // Только левый канал
#define RECORD_TIME     1               // Длительность записи в секундах

// --- Размер одного сэмпла ---
#define BYTES_PER_SAMPLE (BITS_PER_SAMPLE / 8)

// --- Расчёт длины данных ---
#define SAMPLES_COUNT  (SAMPLE_RATE * RECORD_TIME)         // Размер дискретизоного аудиосигнала (16000 точек).
#define DATA_SIZE      (SAMPLES_COUNT * BYTES_PER_SAMPLE)  // Размер аудио-данных в байтах (не включая заголовок).
#define WAV_HEADER_SIZE 44                                 // Размер заголовка в байтах.

// --- Буфер для хранения WAV: заголовок (44 байта) + данные ---
uint8_t wav_buffer[WAV_HEADER_SIZE + DATA_SIZE];

// --- Временный буфер для чтения из I2S ---
int16_t i2s_buffer[256]; // промежуточный буфер


/** В документе реализлваны функции для обработки аудио и построения спектрограмы. **/
#include <Audio_processing.h>
/** В документе реализлваны функции для подключения микрофона и записи аудио. **/
#include <Audio_recording.h>


// ===============================
// Настройка
// ===============================
void setup() {
  // Инициализирует последовательный порт (UART) для связи ESP32 ↔ компьютер.
  Serial.begin(115200);
  Serial.println("I2S microphone record demo");
  // Инициализация I2S
  i2s_install();
  // Настройка пинов I2S
  i2s_setpin();
  // Запускает периферию I2S в режиме приёма/передачи данных.
  i2s_start(I2S_PORT);
  // Задержка 500 миллисекунд.
  delay(500);


  // ===============================
  // Подключение к WiFi.
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  // Вывод IP адреса в последовательный порт.
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());


  /** Инициализация вебсервера (загружаем HTML страничку на WebServer, делаем её корневой "/"):
        + "/" - корневая папка, 
        + HTTP_GET - HTTP-метод GET для запроса данных с сервера
        + [](AsyncWebServerRequest *request) {} - лямбда-функция
        + AsyncWebServerRequest *request - указатель на объект, 
          который содержит всю информацию о запросе, поступившем на сервер.**/
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // отправить (200 - http код ответа, метод отправки по html, HTML страничка)
    request -> send(200, "text\html", getHTML());
  });

  // Запуск вебсокета.
  webSocket.begin();
  // При приёме данных от клиента контролером через соеденение вебсокетов передать данные функцие webSocketEvent для дальнейшей обработки.
  // Функция webSocketEvent реализована в документе "receiveData.h".
  webSocket.onEvent(webSocketEvent);

  // Запуск вебсервера.
  server.begin();
}


void loop() {
  /** Метод webSocket.loop() обеспечивает:
    - Поддержание активного соединения с клиентами.
    - Обработку входящих данных от клиентов.
    - Обработку новых подключений и отключений клиентов.
    - Отправку данных клиентам, если это необходимо.**/
  webSocket.loop();

  // Если пользователь нажал кнопку "DATASET" на веб-страничке.
  if (dataset) {
    // Прописываем WAV-заголовок в буфере wav_buffer.
    create_wav_header(wav_buffer, DATA_SIZE);

    Serial.println("Recording 1 second...");
    // Записываем 1 секунду аудио в буфере wav_buffer.
    record_to_buffer();
    Serial.println("Done!");

    //  - uint8_t * d_buff: Указатель на исходное тихое аудио.
    uint8_t* src  = wav_buffer + WAV_HEADER_SIZE;    // исходные данные (ADC-формат 12 бит)
    //  - uint8_t* s_buff: Указатель для запизи громкого аудио.
    uint8_t* dest = wav_buffer + WAV_HEADER_SIZE;    // будем конвертировать "на месте"
    // Функция увеличивает громкость аудио.
    audio_scale(dest, src, DATA_SIZE);
    Serial.println("Volume scaled!");

    // Указатель на аудиосигнал преобразуемый в спектрограму.
    int16_t *pcm16 = (int16_t*)(wav_buffer + WAV_HEADER_SIZE);

    // Вычисляем спектрограмму и отправляем JPEG-изображение на веб-страничку.
    process_audio_to_spectrogram(pcm16, SAMPLES_COUNT);
    
    Serial.println("Spectrogram Done!");

    //  Размер WAV-аудио в байтах.
    size_t dataLen = WAV_HEADER_SIZE + DATA_SIZE;
    // Отправляем WAV-аудио на веб-страничку.
    sendJson(jsonString, doc_tx, "change_img_type", 1);
    webSocket.broadcastBIN(wav_buffer, dataLen);
    
    Serial.printf("WAV (%u bytes) sent via WebSocket!\n", dataLen);

    // Сбрасывает флаг dataset, чтобы не начать запись снова.
    dataset = false;
  }
}


