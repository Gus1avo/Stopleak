#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiClient.h>

// =====================================================
// SISTEMA EMBARCADO DE DETECÇÃO DE VAZAMENTOS - STOPLEAK
// ESP32 + YF-S201 + BOMBA D'ÁGUA + VÁLVULA SOLENOIDE + LEDS
// =====================================================

#define SENSOR_PIN 17      // Sensor de vazão YF-S201
#define BOMBA_PIN 23       // Relé 1: Bomba d'Água (LOW = LIGADO / HIGH = DESLIGADO)
#define VALVULA_PIN 22     // Relé 2: Válvula Solenoide (LOW = ABERTA / HIGH = FECHADA)

#define LED_BOMBA_PIN 18   // LED de emergência da Bomba
#define LED_VALVULA_PIN 19 // LED de emergência da Válvula

const char *ssid = "StopLeak";
const char *password = "stopleak";

WiFiServer server(80);

// =====================================================
// CALIBRAÇÃO E CONFIGURAÇÕES
// =====================================================
float PULSOS_POR_LITRO = 450.0;
float VAZAO_MINIMA = 0.05;              
unsigned long TEMPO_VAZAMENTO = 10000;  // 10 segundos

// =====================================================
// VARIÁVEIS DE ESTADO
// =====================================================
volatile unsigned long pulsos = 0;
volatile unsigned long ultimoTempoPulso = 0;

unsigned long ultimoCalculo = 0;
unsigned long inicioFluxo = 0;
uint8_t leiturasSemFluxo = 0;

float vazao = 0.0;
float volumeTotal = 0.0;
int contagemAlertas = 0;

bool fluxoDetectado = false;
bool vazamentoDetectado = false;

bool modoAutomatico = true;
bool bombaLigada = true;
bool valvulaAberta = true;

// =====================================================
// INTERRUPÇÃO DO SENSOR DE VAZÃO
// =====================================================
void IRAM_ATTR contarPulso() {
  unsigned long tempoAtual = millis();
  if (tempoAtual - ultimoTempoPulso > 3) {
    pulsos++;
    ultimoTempoPulso = tempoAtual;
  }
}

// =====================================================
// FUNÇÕES DE SEGURANÇA E COMANDO
// =====================================================
void ativarModoAutomatico() {
  modoAutomatico = true;
  vazamentoDetectado = false; 
  fluxoDetectado = false;
  leiturasSemFluxo = 0;
  
  digitalWrite(BOMBA_PIN, LOW);   // LIGA BOMBA
  digitalWrite(VALVULA_PIN, LOW); // ABRE VÁLVULA
  digitalWrite(LED_BOMBA_PIN, LOW);
  digitalWrite(LED_VALVULA_PIN, LOW);

  bombaLigada = true;
  valvulaAberta = true;
  Serial.println("\n--- MODO AUTOMATICO ATIVADO ---");
}

void desligarSistema() {
  modoAutomatico = false;

  digitalWrite(BOMBA_PIN, HIGH);   // DESLIGA BOMBA
  digitalWrite(VALVULA_PIN, HIGH);  // FECHA VÁLVULA
  digitalWrite(LED_BOMBA_PIN, HIGH);
  digitalWrite(LED_VALVULA_PIN, HIGH);

  bombaLigada = false;
  valvulaAberta = false;
  Serial.println("\n--- BLOQUEIO DE EMERGENCIA ATIVADO ---");
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  pinMode(LED_BOMBA_PIN, OUTPUT);
  pinMode(LED_VALVULA_PIN, OUTPUT);
  digitalWrite(LED_BOMBA_PIN, LOW);
  digitalWrite(LED_VALVULA_PIN, LOW);

  Serial.begin(115200);

  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(BOMBA_PIN, OUTPUT);
  pinMode(VALVULA_PIN, OUTPUT);

  ativarModoAutomatico();

  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), contarPulso, RISING);

  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  server.begin();

  Serial.println("\n========================================");
  Serial.println(" DASHBOARD STOPLEAK ON-LINE (DESKTOP VERSION)");
  Serial.print("Acesse: http://"); Serial.println(IP);
  Serial.println("Digite 't' no Serial para simular o teste");
  Serial.println("========================================\n");
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================
void loop() {
  unsigned long agora = millis();

  // VERIFICAÇÃO DO COMANDO "t" VIA MONITOR SERIAL
  if (Serial.available() > 0) {
    char comando = Serial.read();
    if (comando == 't' || comando == 'T') {
      Serial.println("\n[TESTE SERIAL INICIADO] Simulando vazamento...");
      for (int i = 1; i <= 5; i++) {
        Serial.print("Contagem: ");
        Serial.print(i);
        Serial.println("s");
        delay(1000);
      }
      Serial.println("[TESTE] Tempo atingido! Disparando vazamento...");
      vazamentoDetectado = true;
      contagemAlertas++;
      desligarSistema();
    }
  }

  // Trava de hardware contínua se houver vazamento
  if (vazamentoDetectado) {
    digitalWrite(BOMBA_PIN, HIGH);
    digitalWrite(VALVULA_PIN, HIGH);
    digitalWrite(LED_BOMBA_PIN, HIGH);
    digitalWrite(LED_VALVULA_PIN, HIGH);
    bombaLigada = false;
    valvulaAberta = false;
  }

  // Cálculo de Vazão a cada 1 segundo
  if (agora - ultimoCalculo >= 1000) {
    ultimoCalculo = agora;

    noInterrupts();
    unsigned long pulsosAtual = pulsos;
    pulsos = 0;
    interrupts();

    vazao = (pulsosAtual * 60.0) / PULSOS_POR_LITRO;
    volumeTotal += (pulsosAtual / PULSOS_POR_LITRO);

    if (modoAutomatico && !vazamentoDetectado) {
      if (vazao >= VAZAO_MINIMA) {
        leiturasSemFluxo = 0;

        if (!fluxoDetectado) {
          fluxoDetectado = true;
          inicioFluxo = agora;
        }

        unsigned long tempoFluxo = agora - inicioFluxo;

        if (tempoFluxo >= TEMPO_VAZAMENTO) {
          vazamentoDetectado = true;
          contagemAlertas++;
          desligarSistema();
        }
      } else {
        if (fluxoDetectado) {
          leiturasSemFluxo++;
          if (leiturasSemFluxo >= 3) {
            fluxoDetectado = false;
            leiturasSemFluxo = 0;
          }
        }
      }
    }
  }

  // Atendimento às requisições do Servidor Web
  WiFiClient client = server.available();
  if (client) {
    String req = client.readStringUntil('\r');
    client.flush();

    // Rota da API JSON para atualização em tempo real
    if (req.indexOf("GET /api/status") >= 0) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      
      String json = "{";
      json += "\"vazao\":" + String(vazao, 1) + ",";
      json += "\"volume\":" + String(volumeTotal, 2) + ",";
      json += "\"valvula\":" + String(valvulaAberta ? "true" : "false") + ",";
      json += "\"bomba\":" + String(bombaLigada ? "true" : "false") + ",";
      json += "\"vazamento\":" + String(vazamentoDetectado ? "true" : "false") + ",";
      json += "\"alertas\":" + String(contagemAlertas) + ",";
      json += "\"modoAuto\":" + String(modoAutomatico ? "true" : "false");
      json += "}";
      
      client.println(json);
      client.stop();
      return;
    }

    // Rotas de controle via Ações
    if (req.indexOf("GET /valvula/toggle") >= 0) {
      if (!vazamentoDetectado) {
        valvulaAberta = !valvulaAberta;
        digitalWrite(VALVULA_PIN, valvulaAberta ? LOW : HIGH);
        digitalWrite(LED_VALVULA_PIN, valvulaAberta ? LOW : HIGH);
      }
    } else if (req.indexOf("GET /bomba/toggle") >= 0) {
      if (!vazamentoDetectado) {
        bombaLigada = !bombaLigada;
        digitalWrite(BOMBA_PIN, bombaLigada ? LOW : HIGH);
        digitalWrite(LED_BOMBA_PIN, bombaLigada ? LOW : HIGH);
      }
    } else if (req.indexOf("GET /modo/auto") >= 0) {
      ativarModoAutomatico();
    } else if (req.indexOf("GET /simular/vazamento") >= 0) {
      vazamentoDetectado = true;
      contagemAlertas++;
      desligarSistema();
    }

    // Resposta com a Interface HTML replicada e Expandida para Desktop
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();

    client.println("<!DOCTYPE html><html lang='pt-BR'><head>");
    client.println("<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>");
    client.println("<title>StopLeak - Painel de Controle Desktop</title>");
    client.println("<style>");
    client.println("* { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }");
    client.println("body { background-color: #f2f7fa; color: #1e293b; padding-bottom: 50px; }");
    
    // HEADER DESKTOP EXPANDIDO
    client.println("header { background: #fff; padding: 16px 8%; display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid #e2e8f0; box-shadow: 0 1px 3px rgba(0,0,0,0.02); }");
    client.println(".logo { display: flex; align-items: center; gap: 10px; font-weight: bold; font-size: 22px; color: #0284c7; }");
    client.println("nav a { text-decoration: none; color: #475569; margin-left: 28px; font-size: 15px; font-weight: 500; transition: color 0.2s; }");
    client.println("nav a:hover { color: #0284c7; }");
    client.println("nav a.active { background: #0284c7; color: white; padding: 8px 22px; border-radius: 20px; }");
    
    // CONTAINER WIDESCREEN
    client.println(".container { max-width: 1400px; margin: 40px auto; padding: 0 30px; }");
    client.println("h1 { font-size: 28px; margin-bottom: 30px; color: #0f172a; font-weight: 700; }");
    
    // GRID DE 3 COLUNAS PARA DESKTOP
    client.println(".grid-cards { display: grid; grid-template-columns: repeat(3, 1fr); gap: 24px; margin-bottom: 28px; }");
    client.println(".card { background: white; border-radius: 16px; padding: 24px; border: 1px solid #e2e8f0; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.03); display: flex; flex-direction: column; justify-content: space-between; }");
    client.println(".card-header { font-size: 14px; color: #64748b; font-weight: 600; margin-bottom: 16px; display: flex; justify-content: space-between; align-items: center; }");
    client.println(".card-title { font-size: 32px; font-weight: bold; color: #0f172a; margin-bottom: 6px; display: flex; align-items: center; gap: 12px; }");
    client.println(".card-sub { font-size: 13px; color: #94a3b8; }");
    client.println(".dot { width: 14px; height: 14px; border-radius: 50%; display: inline-block; }");
    client.println(".dot-green { background: #22c55e; }");
    client.println(".dot-red { background: #ef4444; }");
    client.println(".icon-bg { background: #f0fdf4; color: #16a34a; padding: 8px; border-radius: 50%; width: 40px; height: 40px; display: flex; align-items: center; justify-content: center; font-size: 18px; }");
    
    // GRID DE AÇÕES LADO A LADO EM TELAS GRANDES
    client.println(".grid-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }");
    
    // ESTILOS DE BOTÕES
    client.println(".btn { border: none; padding: 14px 24px; border-radius: 10px; font-weight: 600; cursor: pointer; transition: all 0.2s; font-size: 15px; text-decoration: none; text-align: center; flex: 1; }");
    client.println(".btn-red { background: #b91c1c; color: white; }");
    client.println(".btn-red:hover { background: #991b1b; }");
    client.println(".btn-green { background: #15803d; color: white; }");
    client.println(".btn-green:hover { background: #166534; }");
    
    client.println(".btn-sim { border: 1px solid #cbd5e1; flex: 1; text-align: left; padding: 16px; border-radius: 12px; cursor: pointer; transition: all 0.2s; background: #f1f5f9; color: #334155; }");
    client.println(".btn-sim-blue { background: #0284c7 !important; color: white !important; border-color: #0284c7 !important; }");
    client.println(".btn-sim-red { background: #b91c1c !important; color: white !important; border-color: #b91c1c !important; }");
    client.println(".btn-sim-gray { background: #f1f5f9 !important; color: #64748b !important; border-color: #e2e8f0 !important; }");
    client.println(".sim-group { display: flex; gap: 16px; margin-top: 12px; }");
    
    // MEDIA QUERY PARA DISPOSITIVOS MENORES (TABLETS / PHONES)
    client.println("@media (max-width: 1024px) { .grid-cards { grid-template-columns: repeat(2, 1fr); } }");
    client.println("@media (max-width: 768px) { .grid-cards, .grid-actions { grid-template-columns: 1fr; } header { padding: 12px 20px; } nav { display: none; } }");
    
    client.println("footer { text-align: center; margin-top: 50px; font-size: 14px; color: #94a3b8; }");
    client.println("</style></head><body>");

    client.println("<header><div class='logo'>💧 StopLeak</div><nav><a class='active' href='#'>Painel</a><a href='#'>Monitoramento</a><a href='#'>Histórico</a><a href='#'>Configurações</a></nav></header>");
    client.println("<div class='container'><h1>Painel de controle</h1>");
    
    // GRID DE CARDS EM 3 COLUNAS
    client.println("<div class='grid-cards'>");
    
    // Status do Sistema
    client.println("<div class='card'><div class='card-header'>Status do sistema <span class='icon-bg'>✓</span></div>");
    client.println("<div class='card-title'><span id='dotStatus' class='dot dot-green'></span><span id='textStatus'>Normal</span></div>");
    client.println("<div class='card-sub' id='subStatus'>Nenhuma anomalia nos últimos minutos</div></div>");

    // Vazão Atual
    client.println("<div class='card'><div class='card-header'>Vazão atual <span class='icon-bg'>📈</span></div>");
    client.println("<div class='card-title'><span id='valVazao'>0,0</span> L/min</div>");
    client.println("<div class='card-sub'>Leitura ao vivo do sensor YF-S201</div></div>");

    // Estado da Válvula
    client.println("<div class='card'><div class='card-header'>Estado da válvula <span class='icon-bg'>♒</span></div>");
    client.println("<div class='card-title'><span id='dotValvula' class='dot dot-green'></span><span id='textValvula'>Aberta</span></div>");
    client.println("<div class='card-sub' id='subValvula'>Água circulando</div></div>");

    // Estado da Bomba
    client.println("<div class='card'><div class='card-header'>Estado da bomba <span class='icon-bg'>⏻</span></div>");
    client.println("<div class='card-title'><span id='dotBomba' class='dot dot-green'></span><span id='textBomba'>Ligada</span></div>");
    client.println("<div class='card-sub' id='subBomba'>Pressurizando o sistema</div></div>");

    // Dispositivo ESP32
    client.println("<div class='card'><div class='card-header'>Dispositivo ESP32 <span class='icon-bg'>📶</span></div>");
    client.println("<div class='card-title'><span class='dot dot-green'></span> Conectado</div>");
    client.println("<div class='card-sub'>Wi-Fi estável · sinal bom</div></div>");

    // Alertas Registrados
    client.println("<div class='card'><div class='card-header'>Alertas registrados <span class='icon-bg'>🔔</span></div>");
    client.println("<div class='card-title'><span id='valAlertas'>0</span> alertas</div>");
    client.println("<div class='card-sub' id='subAlertas'>Nenhum alerta ativo</div></div>");

    client.println("</div>");

    // LINHA DE AÇÕES RÁPIDAS E SIMULAÇÃO LADO A LADO
    client.println("<div class='grid-actions'>");

    // AÇÕES RÁPIDAS
    client.println("<div class='card'><div class='card-header'>Ações rápidas</div>");
    client.println("<p class='card-sub' style='margin-bottom:18px;'>Ações críticas executadas diretamente nos atuadores do protótipo.</p>");
    client.println("<div style='display:flex; gap:16px;'>");
    client.println("<button id='btnValvula' onclick='executar(\"/valvula/toggle\")' class='btn btn-red'>Fechar válvula</button>");
    client.println("<button id='btnBomba' onclick='executar(\"/bomba/toggle\")' class='btn btn-red'>Desligar bomba</button>");
    client.println("</div></div>");

    // SIMULAÇÃO E CONTROLE
    client.println("<div class='card'><div class='card-header'>Simulação e Controle do Sistema</div>");
    client.println("<p class='card-sub' style='margin-bottom:6px;'>Use os cenários abaixo para alternar os estados do protótipo.</p>");
    client.println("<div class='sim-group'>");
    client.println("<button id='btnAuto' onclick='executar(\"/modo/auto\")' class='btn-sim btn-sim-blue'><b>Fluxo normal</b><br><small>Modo Automático</small></button>");
    client.println("<button id='btnVazamento' onclick='executar(\"/simular/vazamento\")' class='btn-sim btn-sim-gray'><b>Disparar vazamento</b><br><small>Simula fluxo contínuo</small></button>");
    client.println("</div></div>");

    client.println("</div>");

    client.println("<footer>StopLeak — monitoramento inteligente de vazamentos</footer></div>");

    // JAVASCRIPT AJAX PARA ATUALIZAÇÃO DINÂMICA
    client.println("<script>");
    client.println("function atualizar() {");
    client.println("  fetch('/api/status').then(r => r.json()).then(data => {");
    client.println("    document.getElementById('valVazao').innerText = data.vazao.toFixed(1).replace('.', ',');");
    client.println("    document.getElementById('valAlertas').innerText = data.alertas;");
    
    // Status do Sistema
    client.println("    if(data.vazamento) {");
    client.println("      document.getElementById('textStatus').innerText = 'ALERTA!';");
    client.println("      document.getElementById('dotStatus').className = 'dot dot-red';");
    client.println("      document.getElementById('subStatus').innerText = 'Vazamento contínuo detectado!';");
    client.println("      document.getElementById('subAlertas').innerText = 'Bloqueio de emergência ativo';");
    client.println("    } else {");
    client.println("      document.getElementById('textStatus').innerText = 'Normal';");
    client.println("      document.getElementById('dotStatus').className = 'dot dot-green';");
    client.println("      document.getElementById('subStatus').innerText = 'Nenhuma anomalia nos últimos minutos';");
    client.println("      document.getElementById('subAlertas').innerText = 'Nenhum alerta ativo';");
    client.println("    }");

    // Botão e Status Válvula
    client.println("    let btnV = document.getElementById('btnValvula');");
    client.println("    if(data.valvula) {");
    client.println("      document.getElementById('textValvula').innerText = 'Aberta';");
    client.println("      document.getElementById('dotValvula').className = 'dot dot-green';");
    client.println("      document.getElementById('subValvula').innerText = 'Água circulando';");
    client.println("      btnV.innerText = 'Fechar válvula';");
    client.println("      btnV.className = 'btn btn-red';");
    client.println("    } else {");
    client.println("      document.getElementById('textValvula').innerText = 'Fechada';");
    client.println("      document.getElementById('dotValvula').className = 'dot dot-red';");
    client.println("      document.getElementById('subValvula').innerText = 'Bloqueada';");
    client.println("      btnV.innerText = 'Abrir válvula';");
    client.println("      btnV.className = 'btn btn-green';");
    client.println("    }");

    // Botão e Status Bomba
    client.println("    let btnB = document.getElementById('btnBomba');");
    client.println("    if(data.bomba) {");
    client.println("      document.getElementById('textBomba').innerText = 'Ligada';");
    client.println("      document.getElementById('dotBomba').className = 'dot dot-green';");
    client.println("      document.getElementById('subBomba').innerText = 'Pressurizando o sistema';");
    client.println("      btnB.innerText = 'Desligar bomba';");
    client.println("      btnB.className = 'btn btn-red';");
    client.println("    } else {");
    client.println("      document.getElementById('textBomba').innerText = 'Desligada';");
    client.println("      document.getElementById('dotBomba').className = 'dot dot-red';");
    client.println("      document.getElementById('subBomba').innerText = 'Despressurizada';");
    client.println("      btnB.innerText = 'Ligar bomba';");
    client.println("      btnB.className = 'btn btn-green';");
    client.println("    }");

    // Estilos dos Botões de Simulação
    client.println("    let btnAuto = document.getElementById('btnAuto');");
    client.println("    let btnVaz = document.getElementById('btnVazamento');");
    client.println("    if(data.vazamento) {");
    client.println("      btnAuto.className = 'btn-sim btn-sim-gray';");
    client.println("      btnVaz.className = 'btn-sim btn-sim-red';");
    client.println("    } else {");
    client.println("      btnAuto.className = 'btn-sim btn-sim-blue';");
    client.println("      btnVaz.className = 'btn-sim btn-sim-gray';");
    client.println("    }");

    client.println("  });");
    client.println("}");
    client.println("function executar(rota) { fetch(rota).then(() => atualizar()); }");
    client.println("setInterval(atualizar, 1000);");
    client.println("</script></body></html>");

    client.stop();
  }
}   
