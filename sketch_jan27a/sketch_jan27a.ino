#include <pt.h>
#include <SevSeg.h>

static bool emergency_on = false;
static bool is_timer_off = false;
static bool red_on = false;
static bool is_timer_ended = false;
static bool is_written = false;
static bool emergency_timer_started = false;

static const int RED_TIME = 20000; // In mseconds
static const int REDYELLOW_TIME = 2000; // In mseconds
static const int YELLOW_TIME = 5000; // In mseconds
static const int GREEN_TIME = 30000; // In mseconds

struct timer {int start, interval;};
static int timer_expired(struct timer *t);
static void timer_set(struct timer *t, int msecs);

static struct pt traffic_pt, segment_pt, emergency_pt;

static const int red_pin = 2;
static const int yellow_pin = 3;
static const int green_pin = 4;
static const int btn_pin = 5;

static bool get_emergency_timer_started()
{
  return emergency_timer_started;
}

static bool set_emergency_timer_started(bool new_state)
{
  emergency_timer_started = new_state;
}

static bool emergency_triggered()
{
  return emergency_on;  
}

static void set_emergency_triggered(bool new_state)
{
  emergency_on = new_state;  
}

static bool can_proceed()
{
  return is_written;
}

static void set_can_proceed(bool new_status)
{
  is_written = new_status;
}

static bool timer_off()
{
  return is_timer_off;  
}

static void set_timer_off(bool new_status) 
{
  is_timer_off = new_status;  
}

static const byte numberofDigits = 4;

// Pins for decimal point and each segment
// A, B, C, D, E, F, G, DP
byte segmentPins[] = { 39, 25, 37, 33, 31, 41, 29, 35 };
// Digits 1, 2, 3, 4
byte digitPins[] = { 45, 47, 49, 51 };
bool resistorsOnSegments = true;
byte hardwareConfig = NP_COMMON_ANODE;

SevSeg sevseg;

static PT_THREAD(traffic_thread(struct pt *pt))
{  
  PT_BEGIN(pt);

  while(1) {
    
    if (!red_on) {
      // Will be here to optimize delay
      digitalWrite(yellow_pin, LOW);
      set_timer_off(false);
      digitalWrite(red_pin, HIGH);
      PT_WAIT_UNTIL(pt, timer_off() || emergency_triggered());
      
      if(emergency_triggered()) {
        PT_WAIT_UNTIL(pt, get_emergency_timer_started());
        
        digitalWrite(red_pin, LOW);        
        digitalWrite(green_pin, HIGH);
        set_emergency_timer_started(false);
        PT_WAIT_UNTIL(pt, timer_off());
        digitalWrite(green_pin, LOW);
      }
      
      set_timer_off(false);
      
      digitalWrite(yellow_pin, HIGH);
      PT_WAIT_UNTIL(pt, timer_off());
      
      set_timer_off(false);
    }  else {
      // Will be here to optimize the delay before greens go ON
      digitalWrite(red_pin, LOW);
      digitalWrite(yellow_pin, LOW);
      
      digitalWrite(green_pin, HIGH);
      PT_WAIT_UNTIL(pt, timer_off());
      
      if(emergency_triggered()) {
        PT_WAIT_UNTIL(pt, get_emergency_timer_started());      
        set_timer_off(false);
        digitalWrite(green_pin, HIGH);
        set_emergency_timer_started(false);
        PT_WAIT_UNTIL(pt, timer_off());
      }
      
      digitalWrite(green_pin, LOW);
      set_timer_off(false);
      
      digitalWrite(yellow_pin, HIGH);
      PT_WAIT_UNTIL(pt, timer_off());
    }
    
    PT_WAIT_UNTIL(pt, !can_proceed());
    red_on = !red_on;
    
    set_can_proceed(true);
  }

  PT_END(pt);
}

static PT_THREAD(segment_thread(struct pt *pt))
{  
  static unsigned long timestamp = 0;
  static int i = 0;
  static int time_to_wait = RED_TIME / 1000;
  static int time_to_wait_change = REDYELLOW_TIME / 1000;
  
  PT_BEGIN(pt);

  while(1) {
    
    for(i = time_to_wait; i > 0; i--)
    {
      sevseg.setNumber(i);
      PT_WAIT_UNTIL(pt, millis() - timestamp > 1000 || emergency_triggered());
      
      if(emergency_triggered()) {
        time_to_wait = GREEN_TIME / 1000;
        i = time_to_wait;
        // To simulate real-life situation (lights are not changed immidiatly, 
        // thus, drivers have chance to use breaks or pass through) a small delay (~200 ms)
        // will be added
        timestamp = millis(); // take a new timestamp
        PT_WAIT_UNTIL(pt, millis() - timestamp > 200);
        set_emergency_timer_started(true);
        if(red_on)
          set_timer_off(true);
          
        PT_WAIT_UNTIL(pt, !get_emergency_timer_started());
        set_emergency_triggered(false);
      }
      
      timestamp = millis(); // take a new timestamp
    }

    set_timer_off(true);
    
    PT_WAIT_UNTIL(pt, !timer_off());
    
    for(i = time_to_wait_change; i > 0; i = i - 1)
    {
      sevseg.setNumber(0);
      PT_WAIT_UNTIL(pt, millis() - timestamp > 500);
      timestamp = millis(); // take a new timestamp
      sevseg.blank();
      PT_WAIT_UNTIL(pt, millis() - timestamp > 500);
      timestamp = millis(); // take a new timestamp
    }

    set_timer_off(true);
    
    PT_WAIT_UNTIL(pt, can_proceed());

    time_to_wait = (red_on ? GREEN_TIME : RED_TIME) / 1000;
    time_to_wait_change = (red_on ? YELLOW_TIME : REDYELLOW_TIME) / 1000;

    set_can_proceed(false);
  }

  PT_END(pt);
}

static PT_THREAD(emergency_thread(struct pt *pt))
{  
  static unsigned long timestamp = 0;
  static int btn_state = LOW;
  static int i = 0;
  static int time_to_wait = GREEN_TIME / 1000;
  
  PT_BEGIN(pt);

  while(1) {
    btn_state = digitalRead(btn_pin);
    PT_WAIT_UNTIL(pt, millis() - timestamp > 200);
    timestamp = millis();
    
    if (btn_state == HIGH) {
      btn_state = digitalRead(btn_pin);
      if (btn_state == LOW)
        set_emergency_triggered(true);
    }

    PT_WAIT_UNTIL(pt, !emergency_triggered());
  }
  
  PT_END(pt);
}

void setup() {
  // put your setup code here, to run once:
  pinMode(red_pin, OUTPUT);
  pinMode(yellow_pin, OUTPUT);
  pinMode(green_pin, OUTPUT);
  pinMode(btn_pin, INPUT);
  
  sevseg.begin(hardwareConfig, numberofDigits, digitPins, segmentPins, resistorsOnSegments, false, false, true );
  sevseg.setBrightness(30);

  PT_INIT(&traffic_pt);
  PT_INIT(&segment_pt);
  PT_INIT(&emergency_pt);
}

void loop() {
  // put your main code here, to run repeatedly:
  traffic_thread(&traffic_pt);
  segment_thread(&segment_pt);
  sevseg.refreshDisplay(); 
  emergency_thread(&emergency_pt); 
}
