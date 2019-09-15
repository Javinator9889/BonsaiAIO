#ifndef CustomHTML_h
#define CustomHTML_h

const String html = 
"<html>"
"{HEAD}"
"{BODY}"
"</html>";

const String head = 
"<head>"
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
  "<style type=\"text/css\">"
    "body {"
    "-webkit-appearance:none;"
    "-moz-appearance:none;"
    "font-family:'Arial',sans-serif;"
    "text-align:left;"
    "}"
    ".menu > a:link {"
    "position: absolute;"
    "display: inline-block;"
    "right: 12px;"
    "padding: 0 6px;"
    "text-decoration: none;"
    "}"
    ".button {"
    "display:inline-block;"
    "border-radius:7px;"
    "background:#73ad21;"
    "margin:0 10px 0 10px;"
    "padding:10px 20px 10px 20px;"
    "text-decoration:none;"
    "color:#000000;"
    "}"
    ".div {"
    "width:auto;"
    "padding:25px;"
    "}"
  "</style>"
"</head>";

const String body = 
"<body>"
"{BODY}"
"</body>";

const String cogMenu = 
"<div class=\"menu\">" AUTOCONNECT_LINK(COG_24) "</div>";

const String barMenu = 
"<div class=\"menu\">" AUTOCONNECT_LINK(BAR_24) "</div>";

const String setupOptions = 
"{MENU}"
"<div>"
    "<h1>BonsaiAIO settings</h1>"
    "<b>Warning LED</b> - status: <i>"
        "<span style=\"font-weight:bold;color:{LED_STATUS}"
        "</span></i>"
    "<p>"
        "<a class=\"button\" href=\"/io?v=disable\">Disable</a><a class=\"button\" href=\"/io?v=enable\">Enable</a>"
    "</p><br />"
    "<p>"
        "<form action=\"/io\" method=\"get\">"
            "<h2>Sensor options</h2>"
            "<h5>Elapsed time between measurements (seconds)</h5>"
            "<label>Water level sensor</label><br />"
            "<p><input type=\"number\" placeholder=\"Elapsed time...\" value=\"{WLVL_TIME}\" step=\"1\" min=\"3\" name=\"wlvl\"></p>"
            "<label>Temperature & humidity sensor</label><br />"
            "<p><input type=\"number\" placeholder=\"Elapsed time...\" value=\"{TEMP_HUMD_TIME}\" step=\"1\" min=\"3\" name=\"dht\"></p>"
            "<h2>Other options</h2>"
            "<label>Percentage at which LED turns on</label><br />"
            "<p><input type=\"number\" placeholder=\"Percentage\" value=\"{LED_PERCENTAGE}\" step=\"1\" min=\"0\" name=\"perc\"></p>"
            "<label>Display change frecuency (seconds)</label><br />"
            "<p><input type=\"number\" placeholder=\"Elapsed time...\" value=\"{DISPLAY_SECONDS}\" step=\"1\" min=\"5\" name=\"secs\"></p>"
            "<label>Temperature fix (the value will be added to the measured temperature)</label><br />"
            "<p><input type=\"number\" placeholder=\"Decimal temperature fix\" value=\"{TEMP_FIX}\" step=\"0.01\" name=\"temp\"></p>"
            "<p><input type=\"submit\" class=\"button\" value=\"Save settings\"></p>"
        "</form>"
    "</p>"
    "<a class=\"button\" href=\"/_ac\" style=\"text-align:center;\">Go to WiFi settings</a>"
"</div>";
#endif
