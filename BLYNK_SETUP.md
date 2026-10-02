# Blynk dashboard setup

A step by step handout. Follow it in order and tick the boxes.

This is what turns the project from a local network with a card in it into
something an examiner can watch on a phone from across the room. It is the last
missing piece of the word IoT.

```
Time            45 to 60 minutes, most of it clicking rather than coding
You need        a laptop, the gateway on USB, a Blynk account, WiFi
Difficulty      low. Nothing here can break the firmware
Risk            none to the archive. The gateway keeps receiving and keeps
                logging even if every step below fails
```

Do this on a weekday with time to spare, not on demonstration day. The module
you are about to install compiles clean but **has never been executed**, and
everything in this project that had never run turned out to have something
wrong with it.

---

# Before you start

Check three things.

```
[ ] gateway/config.h has WIFI_MODE set to 2
[ ] WIFI_SSID and WIFI_PASS are filled in and the gateway reaches the internet
[ ] the serial monitor shows a real IP after boot, not just the access point
```

`WIFI_MODE 1` is access point only. It has no internet, the dashboard cannot
work, and the module will disable itself and say so once. If you are on 1,
change it to 2 before going further.

Confirm internet by looking for these two lines at boot.

```
WiFi: internet up, IP 172.20.10.2, RSSI -48 dBm
forecast: dry, 0.0 mm/h, 3 min old
```

The second line is the proof. If it says `forecast: none usable` the gateway
associated but has no route out, and Blynk will not connect either. Fix that
first. On an iPhone hotspot the usual causes are Maximize Compatibility being
off, or mobile data being off.

---

# Part 1. The Blynk console

## Step 1. Account

```
[ ] Go to blynk.cloud and create a free account
[ ] Verify the email
```

## Step 2. Template

```
[ ] Developer Zone, then Templates, then New Template
[ ] Name          StormDrain
[ ] Hardware      ESP32
[ ] Connection    WiFi
[ ] Save
```

The name must be exactly `StormDrain`, character for character, because it has
to match `BLYNK_TEMPLATE_NAME` in the code. A mismatch means the device never
binds and the only symptom is that it never connects.

## Step 3. Copy the template ID

```
[ ] Open the template, Info tab
[ ] Copy the Template ID. It looks like TMPL6xxxxxxx
[ ] Paste it somewhere you can find it in ten minutes
```

## Step 4. Create the device

```
[ ] Search, then New Device, then From Template, then StormDrain
[ ] Name it Gateway
[ ] Open Device Info and copy the AuthToken
```

That token is the device password. Treat it like one. Do not commit it to a
public repository and change it after the demonstration if the code is going
into a report that gets shared.

## Step 5. The datastreams

Template, Datastreams tab, New Datastream, Virtual Pin, for each row below.
There are 22.

One global stream.

```
[ ] V0    forecast_mm      Double    0 to 50      units mm
```

Then seven per node. The pattern is the node number times ten, plus 0 to 6.

Node 1.

```
[ ] V10   n1_level         Integer   0 to 5000    units mm
[ ] V11   n1_rate          Integer   -127 to 127  units mm/min
[ ] V12   n1_severity      Integer   0 to 2
[ ] V13   n1_battery       Integer   0 to 100     units %
[ ] V14   n1_trend         Integer   0 to 3
[ ] V15   n1_stalled       Integer   0 to 1
[ ] V16   n1_verdict       Integer   0 to 3
```

Node 2.

```
[ ] V20   n2_level         Integer   0 to 5000    units mm
[ ] V21   n2_rate          Integer   -127 to 127  units mm/min
[ ] V22   n2_severity      Integer   0 to 2
[ ] V23   n2_battery       Integer   0 to 100     units %
[ ] V24   n2_trend         Integer   0 to 3
[ ] V25   n2_stalled       Integer   0 to 1
[ ] V26   n2_verdict       Integer   0 to 3
```

Node 3.

```
[ ] V30   n3_level         Integer   0 to 5000    units mm
[ ] V31   n3_rate          Integer   -127 to 127  units mm/min
[ ] V32   n3_severity      Integer   0 to 2
[ ] V33   n3_battery       Integer   0 to 100     units %
[ ] V34   n3_trend         Integer   0 to 3
[ ] V35   n3_stalled       Integer   0 to 1
[ ] V36   n3_verdict       Integer   0 to 3
```

Check your plan's datastream allowance before you begin. You need 22 and the
free tier is not unlimited. If you run out, drop the three rate streams and the
three battery streams. Rate is not used by any decision and battery reads zero
until the dividers are wired.

## What the coded values mean

Write this on the whiteboard for the viva.

```
severity    0 normal       1 elevated      2 waterlogged
trend       0 steady       1 rising        2 receding      3 stalled
verdict     0 none         1 storm         2 suspected     3 confirmed
```

The verdict is the interesting one and it is the gateway's judgement, not the
node's measurement. A node can only measure that water is not going down. The
gateway decides whether that is a blocked drain or a storm.

## Step 6. The three events

Template, Events tab, New Event, for each. The event code must be **exactly**
as written, all lowercase. A wrong case fails silently. No error, no alert,
nothing at all.

```
[ ] code  waterlogging    name  Waterlogging      type  Warning
[ ] code  blockage        name  Blockage          type  Critical
[ ] code  node_offline    name  Node offline      type  Warning
```

For each one, open its Notifications tab.

```
[ ] blockage       turn Push Notification ON. This is the one that matters
[ ] waterlogging   Push ON if you want it
[ ] node_offline   Push ON. A silent node during a demonstration is worth knowing
```

Save the template.

---

# Part 2. The code

## Step 7. Install the library

```
[ ] Arduino IDE, Tools, Manage Libraries
[ ] Search Blynk
[ ] Install "Blynk" BY BLYNK, version 1.3.5. Not the Khoi Hoang forks, which
    do not provide BlynkSimpleEsp32.h
```

## Step 8. Fill in secrets.h

The dashboard build is already in `gateway/net_report.cpp`. It reads its
credentials from `gateway/secrets.h`, which is not in the repository.

```
[ ] Copy gateway/secrets.example.h to gateway/secrets.h
[ ] Set WIFI_SSID and WIFI_PASS to your 2.4 GHz hotspot
[ ] Set BLYNK_TEMPLATE_ID from Step 3
[ ] Set BLYNK_TEMPLATE_NAME to match Step 2 exactly
[ ] Set BLYNK_AUTH_TOKEN from Step 4
```

## Step 9. Install it

```
[ ] Compile
[ ] Upload to the gateway
```

---

# Part 3. The dashboard

Web dashboard first, because it is easier to lay out, then Mobile.

## Step 10. The widgets that matter

```
[ ] Three large Label or LED widgets on V16, V26 and V36
```

That is the verdict for each node and it is the first thing anyone should see.
Colour them so 3 shows red. If you use Labels, set the suffix so they read as
words rather than numbers.

```
[ ] One Chart with three series, V10 and V20 and V30
```

All three water levels on one timeline. This is your demonstration. When you
raise one vessel the group watches one line climb while the other two sit flat.

```
[ ] One Gauge on V0 for rainfall in mm
[ ] Three small Value widgets on V13, V23, V33 for battery, for later
```

Optional but useful for the viva.

```
[ ] Three LEDs on V15, V25, V35 so you can see the raw stall separately from
    the verdict. That pair is what shows the node measuring and the gateway
    judging as two different things
```

---

# Part 4. First run

Power cycle the gateway with the serial monitor open.

## What you should see, in this order

```
gateway ready: 433000000 Hz, SF8, sync 0x37, SD logging
WiFi: connecting to "your-hotspot-name"
Blynk: configured, connecting in the background
WiFi: internet up, IP 172.20.10.2, RSSI -48 dBm
Blynk: connected
```

```
[ ] "Blynk: configured" appears at boot
[ ] "Blynk: connected" appears within about 30 seconds
[ ] the device shows Online in the Blynk console
[ ] numbers start moving on the dashboard as packets arrive
```

The gateway never blocks waiting for Blynk. It uses `Blynk.config()` rather
than `Blynk.begin()` on purpose, so a wrong token gives you a gateway that still
receives and still archives and simply prints a retry line every 30 seconds. The
archive must survive a dead dashboard.

## Prove the alerts

```
[ ] Raise one vessel past LEVEL_SAFE_MM. Expect a Waterlogging notification
[ ] Hold it above LEVEL_HIGH_MM without a 30 mm drop until the node reports
    BLOCKED, then expect a Blockage notification on the gateway's verdict
[ ] Power a node off and wait NODE_TIMEOUT_MS. Expect a Node offline
    notification. Power it back on and expect the recovery one
```

In bringup mode the stall takes about two minutes and the offline timeout is
two minutes. In field mode they are twenty each, so do this in bringup.

Alerts fire on escalation only, never on every packet. One notification every
five seconds during an event is how people learn to turn notifications off.

Note that `storm` deliberately does not raise an alert. A storm is not a
maintenance callout. The verdict enum orders as none, storm, suspected,
confirmed, and only suspected and confirmed alert.

---

# Part 5. When it does not work

```
Symptom                              Cause and fix
```

`Blynk: configured` appears but
`Blynk: connected` never does.
The token or the template ID is wrong, or the template name does not match.
Copy all three again. This is by far the most common failure.

Device shows Offline in the console
while serial says connected.
You are looking at a different device. Check you created the device from the
StormDrain template and are using that device's token.

Numbers never move on the dashboard
but the archive is filling.
Datastream pin numbers are wrong. Check V10 is node 1 level and not something
else. The pattern is node number times ten.

Some widgets update, others never do.
Those datastreams do not exist, or the virtual pin number is off by one. Blynk
silently drops writes to a pin with no datastream behind it.

No notifications ever arrive.
Event codes are wrong. They must be `waterlogging`, `blockage` and
`node_offline`, all lowercase, spelled exactly. A wrong case fails silently.
Also check Push Notification is actually enabled on the event.

`WiFi: network not found`.
Wrong SSID, or the network is 5 GHz only. The ESP32 has no 5 GHz radio. On an
iPhone turn on Maximize Compatibility.

`WiFi: rejected`.
Wrong password.

Connected to WiFi but
`forecast: none usable`.
Associated with no route to the internet. On a phone hotspot this is mobile
data being off. Blynk will not connect either.

Serial monitor became unreadable
after installing this.
`#define BLYNK_PRINT Serial` at the top of the file sends Blynk's own chatter to
the monitor. Your monitor is deliberately quiet now, reporting only abnormal
nodes, so Blynk buries it. Comment that line out once you are connected.

The gateway resets every minute.
You are on an old build that used `Blynk.begin()`, which blocks while the
watchdog is armed. The module in `gateway/` uses `config()` and cannot do this.

---

# Going back

If you want the build without the dashboard, for calibration or because
something broke.

```
[ ] Copy extras/net_report_base.cpp over gateway/net_report.cpp
[ ] Compile and upload
```

Everything else is untouched. The Blynk module changes nothing outside that one
file.
