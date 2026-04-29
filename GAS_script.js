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

// 1. TELEGRAM BOT CONFIGURATION
// TELEGRAM_TOKEN is the unique key of the bot we created via BotFather.
// It acts as a password allowing this script to send messages on behalf of the bot.
var TELEGRAM_TOKEN = "0000000000:AAAAAAA0000000aaaaaaaa";
// CHAT_ID is the ID of the specific chat where messages should be sent.
var CHAT_ID = "0000000000";

// 2. FUNCTION FOR SENDING A MESSAGE TO TELEGRAM
// This helper function takes a text message and physically sends it via the Telegram API.
function sendToTelegram(text) {
  // Build the exact URL for sending a message via the Telegram API.
  var url = "https://api.telegram.org/bot" + TELEGRAM_TOKEN + "/sendMessage";
  
  // Set the HTTP request parameters for communication with the Telegram server.
  var options = {
    "method": "post",
    "contentType": "application/json",
    "muteHttpExceptions": true,
    "payload": JSON.stringify({ "chat_id": CHAT_ID, "text": text }) // Build the message body (payload).
  };
  
  // Google Apps Script-specific command that performs the actual HTTP request to the internet.
  UrlFetchApp.fetch(url, options);
}

// 3. MAIN FUNCTION FOR RECEIVING DATA FROM TTN
// The doPost(e) function in GAS is triggered automatically every time
// an HTTP POST request arrives at this script's URL.
function doPost(e) {
  // TTN servers send a large amount of data as JSON-formatted text.
  // JSON.parse unpacks this text into a structure that JavaScript can work with.
  var data = JSON.parse(e.postData.contents);
  
  // Retrieve and decode the number sent by our board.
  var number = Utilities.base64Decode(data.uplink_message.frm_payload)[0];

  // A simple lookup table that translates the received number from the board into a text message.
  var messageMap = {
    1: "ALARM: Motion detected (Pin 14)!",
    2: "ALARM: Vibration detected (Pin 25)!",
    3: "ALARM: Sound detected (Pin 13)!"
  };

  // Looks up the correct text from the table based on the received number and calls the send function.
  sendToTelegram(messageMap[number]);
  
  // Response for the TTN server so it knows our script did not fail,
  // otherwise it would start reporting delivery errors.
  return ContentService.createTextOutput("OK");
}