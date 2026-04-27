/*
 * PROJECT: Scrip implementation on script.google.com
 * AUTHOR: Armin Gembal
 * DATE: April 2026
 * DESCRIPTION:
 * Implementation of Google Apps Script for processing incoming data from TTSS.
 * The script decodes the base64 payload from sensors
 * and immediately forwards notifications to Telegram via Bot API.
 * TELEGRAM_TOKEN and CHAT_ID in this file are placeholders. Replace them with real Telegram config.
 */

// 1. KONFIGURACE TELEGRAM BOTA
// TELEGRAM_TOKEN je unikátní klíč bota, kterého jsme si vytvořili přes BotFather.
// Slouží jako heslo, aby tento skript mohl odesílat zprávy jménem bota.
var TELEGRAM_TOKEN = "0000000000:AAAAAAA0000000aaaaaaaa";
// CHAT_ID je ID konkrétního chatu, kam se mají zprávy posílat.
var CHAT_ID = "0000000000";

// 2. FUNKCE PRO ODESLÁNÍ ZPRÁVY NA TELEGRAM
// Tato pomocná funkce vezme textovou zprávu a fyzicky ji odešle přes API Telegramu.
function sendToTelegram(text) {
  // Sestavení přesné URL pro odeslání zprávy přes API Telegramu.
  var url = "https://api.telegram.org/bot" + TELEGRAM_TOKEN + "/sendMessage";
  
  // Nastavení parametrů HTTP požadavku pro komunikaci se serverem Telegramu.
  var options = {
    "method": "post",
    "contentType": "application/json",
    "muteHttpExceptions": true,
    "payload": JSON.stringify({ "chat_id": CHAT_ID, "text": text }) // Vytvoření těla zprávy (payload).
  };
  
  // Specifický příkaz Google Apps Scriptu, který provede samotné odeslání do internetu.
  UrlFetchApp.fetch(url, options);
}

// 3. HLAVNÍ FUNKCE PRO PŘÍJEM DAT Z TTN
// Funkce doPost(e) se v GAS spustí automaticky pokaždé, 
// když na adresu tohoto skriptu přijde nějaký HTTP POST požadavek.
function doPost(e) {
  // Servery TTN posílají spoustu dat jako text ve formátu JSON.
  // JSON.parse tento text rozbalí do struktury, se kterou umí JavaScript dál pracovat.
  var data = JSON.parse(e.postData.contents);
  
  // Získání a dekódování čísla, které posílá naše deska.
  var number = Utilities.base64Decode(data.uplink_message.frm_payload)[0];

  // Jednoduchý slovník, který překládá přijaté číslo z desky na text.
  var messageMap = {
    1: "ALARM: Zjištěn pohyb (Pin 14)!",
    2: "ALARM: Zjištěny vibrace (Pin 25)!",
    3: "ALARM: Zjištěn hluk (Pin 13)!"
  };

  // Vezme správný text ze slovníku podle přijatého čísla a zavolá funkci pro odeslání.
  sendToTelegram(messageMap[number]);
  
  // Odpověď pro server TTN aby věděl, že náš skript neselhal, jinak by začal hlásit chyby s doručováním.
  return ContentService.createTextOutput("OK");
}