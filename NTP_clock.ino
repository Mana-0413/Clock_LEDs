// -+-+-+ヘッダファイルの設定関係 +-+-+-//
#include "arduino_secrets.h"   //ヘッダファイルに接続するWi-fiのSSIDとパスワードが記述されてる
#include "WiFiS3.h"            // Wi-Fiを使うために必要なライブラリ
#include <TimeLib.h>           // 時間を使うために必要なライブラリ
#include <DS3232RTC.h>         // DS3232を使用するのに必要なライブラリ
#include <FastLED.h>           // FastLEDのライブラリ読み込み

DS3232RTC rtc_ds;              // 使用するRTCモジュールのクラスのインスタント化

// -+-+-+使用するピンの設定関係 +-+-+-//
#define led_offset_pos 5               // LEDのスタート位置を決めるもの
#define led_ctrl_pin 13                // LED制御ピンの番号
#define clock_sound_pin 4              // クロック音ピンの番号
#define light_sensor_pin A0            // 光センサ用のピン
#define clock_interval 1000            // クロックの速さを変更
#define light_threshold 1000           // 部屋が暗いか明るいかを決める数（この数以上だと暗い判定）
#define error_led_pin 9                // エラー時に点灯させるLEDピン

int hour_var = 0, min_var = 0, sec_var = 0;  // 時間、分、秒をそれぞれ設定する変数

// -+-+-+LEDの設定関係 +-+-+-//
#define num_leds 60                    // 使うLEDの数を定義。時計は0と60は同じ位置のため60個使用
CRGB leds[num_leds];                   // LEDテープの配列

// 明るいときに点滅するLED色の定義
#define hour_color_center CRGB(250,250,0)      // 短針の真ん中を光らす色
#define hour_color_side CRGB(0,20,0)           // 短針の左右を光らす色
#define minute_color CRGB(0,0,250)             // 長針の場所を光らす色
#define second_color CRGB(250,0,0)             // 秒針の場所を光らす色
#define overlap_color CRGB(250,0,250)          // 長針と秒針が重なったときに光らす色
#define led_off_color CRGB(0,0,0)              // 光らせない
#define scale5_color CRGB(100,100,100)         // 5分ごとに長くなる目盛りの表示

// 暗いときに点滅するLED色の定義
#define hour_dark_color_center CRGB(10,10,0)   // 短針の真ん中を光らす色（暗闇）
#define hour_dark_color_side CRGB(0,10,0)      // 短針の左右を光らす色（暗闇）
#define minute_dark_color CRGB(0,0,20)         // 長針の場所を光らす色（暗闇）
#define scale5_dark_color CRGB(2,2,2)          // 5分ごとに長くなる目盛りの表示（暗闇）

// +-+-+無線LANの接続情報関係(ヘッダファイルにて管理)
char ssid[] = _SSID;                    //ヘッダファイルからSSIDの取得
char pass[] = _PASS;                    //ヘッダファイルからパスワードを取得

// -+-+-+NTPアクセス関係+-+-+-
#define time_zone +900                  // Timezoneを設定（先頭に0を入れると8進数と認識されるため外す）
char ntp_server[] = "pool.ntp.org";     // 使用するNTPサーバ
int ntp_port = 123;                     // 使用するポート番号

// 送受信用バッファの用意
const int ntp_packet_size = 48;
byte buffer[ntp_packet_size];

// UDPクライアントのインスタンス生成
WiFiUDP client;

// +-+-+-デバッグ関係+-+-+-
#define debug_mode true    // デバッグ使用時はtrueに
#define debug_year 2025    // 設定する年
#define debug_month 5      // 設定する月
#define debug_day 14       // 設定する日
#define debug_hour 12      // 設定する時間
#define debug_minute 0     // 設定する分
#define debug_second 0     // 設定する秒

// -+-+-+メイン関数+-+-+-
void setup()
{
  Serial.begin(9600);
  delay(1000);

  FastLED.addLeds<WS2812B, led_ctrl_pin, GRB>(leds, num_leds); // LED初期化
  pinMode(clock_sound_pin, OUTPUT); // クロック音ピン出力
  pinMode(light_sensor_pin, INPUT); // 光センサピン入力
  pinMode(error_led_pin, OUTPUT); // エラーピン出力

  // Wi-Fiモジュール確認
  if (WiFi.status() == WL_NO_MODULE)
  {
    Serial.println("WiFiモジュールがありません。オフラインモードへ移行します。");
    handle_offline_mode();
    return;
  }

  // Wi-fiに接続
  Serial.print("接続中...");
  unsigned long wifi_start = millis();
  while (WiFi.begin(ssid, pass) != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);

    if (millis() - wifi_start > 5000)
    { // タイムアウト
      Serial.println("\nWi-Fi接続失敗。オフラインモードへ移行します。");
      handle_offline_mode();
      return;
    }
  }

  delay(1000);// DHCPからIPアドレスが取得できるまで少し待つ

  // 接続完了
  Serial.println("完了");
  IPAddress ip = WiFi.localIP();
  Serial.print("IPアドレス：");
  Serial.println(ip);

  // IPアドレスが0.0.0.0の場合
  if (ip == INADDR_NONE || ip[0] == 0)
  {
    Serial.println("IPアドレス取得失敗。オフラインモードへ移行します。");
    handle_offline_mode();
    return;
  }

  // NTPで時刻取得
  if (!sync_time_with_ntp())
  {
    Serial.println("NTP取得失敗。オフラインモードへ移行します。");
    handle_offline_mode();
    return;
  }

  //  エラー表示を終了
  digitalWrite(error_led_pin, LOW);
}

//-+-+-+NTPサーバの接続関係+-+-+-
bool sync_time_with_ntp()
{
  client.begin(ntp_port);
  memset(buffer, 0, ntp_packet_size);
  buffer[0] = 0b00001011;

  client.beginPacket(ntp_server, ntp_port);
  client.write(buffer, ntp_packet_size);
  client.endPacket();

  unsigned long start = millis();
  while (!client.parsePacket())
  {
    if (millis() - start > 5000)
      return false;
    delay(100);
  }

  client.read(buffer, ntp_packet_size);
  unsigned long trans_time = buffer[40] << 24 | buffer[41] << 16 | buffer[42] << 8 | buffer[43];
  trans_time += time_zone * 36;

  rtc_ds.begin();
  setTime(trans_time);
  rtc_ds.set(now());
  Serial.println("NTPで取得した時刻をRTCに設定しました:");
  Serial.println(now());
  return true;
}

//  -+-+-+オフラインモード+-+-+-
void handle_offline_mode()
{
  rtc_ds.begin();
  tmElements_t tm;

  // RTCから時刻の読み取り成功
  if (rtc_ds.read(tm))
  {
    // RTCから取得成功
    setTime(makeTime(tm));
    Serial.println("RTCから時刻を読み込みました:");
    Serial.println(now());
  }

  // RTCから時刻の読み取り失敗
  else
  {
    Serial.println("RTC未設定または読み取り失敗");

    // デバッグモードがtrueのとき
    if (debug_mode)
    {
      Serial.println("デバッグモードで時刻を初期化します");
      tm.Year = CalendarYrToTm(debug_year);
      tm.Month = debug_month;
      tm.Day = debug_day;
      tm.Hour = debug_hour;
      tm.Minute = debug_minute;
      tm.Second = debug_second;

      setTime(makeTime(tm));
      rtc_ds.set(now());
      Serial.println("デバッグ時刻をRTCに書き込みました:");
      Serial.println(now());
    }

    // デバッグモードがfalseのとき
    else
    {
      Serial.println("異常終了します");
      digitalWrite(error_led_pin, HIGH);
      while (true);
    }
  }
}

// -+-+-+メイン関数+-+-+-
void loop()
{
  static unsigned long prev_millis = 0;
  unsigned long current_millis = millis();

  if ((long)(current_millis - prev_millis) >= clock_interval) // 1秒ごとに実行
  {
    prev_millis = current_millis;

    set_time();                                               // RTCモジュールから時間を取得
    bool is_bright = is_shine();                              // 明るさの判定

    FastLED.clear();                                          // LED初期化
    display_clock(is_bright);                                 // 時計表示

    if (is_bright)
      shine_mode();                                           //秒針とクロック音をON

    FastLED.show();                                           // LEDの状態を更新
    digitalWrite(clock_sound_pin, LOW);                       // クロック音をOFF
    Serial.println("down");                                   // loop終了確認
  }
}

// -+-+-+cdsセルから値を読み取り、指定した値より明るいか暗いかを確認する関数+-+-+-
bool is_shine()
{
  int cds_sensor_val = analogRead(light_sensor_pin); // 光センサの値を取得
  Serial.print("cdsセル値:");
  Serial.println(cds_sensor_val);
  return cds_sensor_val < light_threshold;
}

// -+-+-+クロック音を鳴らして秒針も表示する関数+-+-+-
void shine_mode()
{
  set_second_led(); // 秒針の表示
  digitalWrite(clock_sound_pin, HIGH);
}

// -+-+-+5分ごとに太くなる針、時針、分針を表示する関数+-+-+-
void display_clock(bool is_bright)
{
  display_scale5(is_bright);      // 5分目盛りの表示
  set_hour_led(is_bright);        // 時針の表示
  set_minute_led(is_bright);      // 分針の表示
}

// -+-+-+RTCモジュールから時間を取得する関数＆デバッグ用に開始時刻を好きなように設定する関数+-+-+-
void set_time()
{
  tmElements_t tm;                //tmElements_tk構造体を宣言
  rtc_ds.read(tm);                //RTCから現在時刻を取得して構造体に代入

  hour_var = tm.Hour;             // 時間
  min_var = tm.Minute;            // 分
  sec_var = tm.Second;            // 秒

  Serial.print(tm.Hour, DEC);     // RTCに設定されてる時間を確認
  Serial.print(":");
  Serial.print(tm.Minute, DEC);   // RTCに設定されている分を確認
  Serial.print(":");
  Serial.println(tm.Second, DEC); // RTCに設定されてる秒を確認
}

// -+-+-+5分ごとに太くなる目盛りを表示する関数+-+-+-
void display_scale5(bool is_bright)
{
  CRGB color = is_bright ? scale5_color : scale5_dark_color;//明るいときと暗いときで光らせるLED色を変化させる
  for (int i = 0; i < num_leds; i += 5)
    leds[get_mapped_pos(i % num_leds)] = color;
}

// -+-+-+LEDのスタート位置を基に点灯する場所を変化させる関数+-+-+-
int get_mapped_pos(int pos)
{
  return (pos + led_offset_pos + num_leds) % num_leds;
}

// -+-+-+時針を設定する関数+-+-+-
void set_hour_led(bool is_bright)
{
  int pos = get_mapped_pos((hour_var % 12) * 5 + min_var / 12);
  CRGB center_color = is_bright ? hour_color_center : hour_dark_color_center; // 明るいときと暗いときで光らせるLED色を変化させる
  CRGB side_color = is_bright ? hour_color_side : hour_dark_color_side;       // 明るいときと暗いときで光らせるLED色を変化させる

  leds[pos] = center_color;                                                   // 時針の場所を表示する

  int left = (pos - 1 + num_leds) % num_leds;
  int right = (pos + 1) % num_leds;

  if (left % 5 != 0)
    leds[left] = side_color;                                                  // サイド針が5分ごとに太くなる目盛りと重なっていないときの設定
  if (right % 5 != 0)
    leds[right] = side_color;                                                 // サイド針が5分ごとに太くなる目盛りと重なっていないときの設定
}

// -+-+-+分針を設定する関数+-+-+-
void set_minute_led(bool is_bright)
{
  CRGB color = is_bright ? minute_color : minute_dark_color;             // 明るいと暗いときで光らせるLED色を変化させる
  leds[get_mapped_pos(min_var)] = color;                                 // 分針の場所を表示する
}

// -+-+-+秒針を設定する関数+-+-+-
void set_second_led()
{
  CRGB color = (sec_var != min_var) ? second_color : overlap_color;      // 秒針だけのときと分針と重なったときで光らせるLED色を変化させる
  leds[get_mapped_pos(sec_var)] = color;                                 // 秒針の場所を表示する
}
