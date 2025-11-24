// Функция подготавливает и возвращает HTML страничку.

String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESP32 + INMP441</title>
  <style>
    body {
      background-color: #EEEEEE;
      font-family: Arial, sans-serif;
      text-align: center;
      margin: 20px;
    }
    button {
      margin: 5px;
      padding: 10px 20px;
      border: none;
      background-color: #4CAF50;
      color: white;
      font-size: 16px;
      border-radius: 6px;
      cursor: pointer;
    }
    button:hover {
      background-color: #45a049;
    }
    .img-container {
      width: 245px;
      height: 100px;
      margin: 10px auto;
      position: relative;
      overflow: hidden;
      border: 2px solid #555;
      border-radius: 10px;
      background-color: #ddd;
    }

    .rotated {
      position: absolute;
      top: 50%;
      left: 50%;
      height: 245px;
      width: 100px;
      transform: translate(-50%, -50%) rotate(-90deg);
      transform-origin: center center;
    }
  </style>
</head>

<body>
  <h2>ESP32 + INMP441</h2>

  <div><b>SPECTROGRAM:</b></div>
  <div class="img-container">
    <img id="imgSPECTROGRAM" class="rotated">
  </div>

  <p>
    <button type="button" id="BTN_dataset">DATASET</button>
    <button type="button" onclick="location.reload();">REFRESH PAGE</button>
  </p>

  <script>
    var Socket;
    var img_type = 0;

    document.getElementById('BTN_dataset').addEventListener('click', button_dataset);

    function init() {
      Socket = new WebSocket('ws://' + window.location.hostname + ':81/');
      Socket.onmessage = function(event) {
        processCommand(event);
      };
    }

    function processCommand(event) {
      if (event.data instanceof Blob) {
        if (img_type == 0) {
          document.getElementById('imgSPECTROGRAM').src = URL.createObjectURL(event.data);
        } else if (img_type == 1) {
          const url = URL.createObjectURL(event.data);
          const a = document.createElement('a');
          a.href = url;
          a.download = 'recording.wav';
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(url);
        } 
      } else {
        try {
          var obj = JSON.parse(event.data);
          var type = obj.type;
          if (type === "change_img_type") {
            img_type = obj.value;
          }
        } catch (e) {
          console.error("Received data is neither Blob nor valid JSON:", event.data);
        }
      }
    }

    function button_dataset() {
      var btn_cpt = {type: 'dataset', value: true};
      Socket.send(JSON.stringify(btn_cpt));
    }

    window.onload = function(event) {
      init();
    };
  </script>
</body>
</html>
)rawliteral";

  return html;
}