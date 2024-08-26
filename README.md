# MiniPillScale
An Arduino project using an Adafruit ESP32-S2 to keep track of medication

Check out the video here:
https://youtu.be/AKGFAqwL43Q

## Deeper details
The code mostly works for 1 pill in the morning right now, but could easily be modified for a couple of the same pills a day. The pill weight should be more than 250mg, as my 500mg pills work fine but I see 10-20% variation.
(There's a calibration step that should be fixed in your code, although I'm not 100% sure you need to if you don't care that grams in the info screen are accurate)

You'll need to enter your wifi secrets in a secrets.h file.

The 3 buttons are for: 
1. Sleep (it auto sleeps after 30 seconds)
2. Setup/calibration
3. Fix pillcount. If you get new pills or forget a day, just click this and it gets you back on track.

Download the 3d printing files here:
https://www.printables.com/model/986309

## The logo
Putting the logo on the Microcontroller was the most frustrating part. I didn't document exactly how to do it, but I had to flash the microcontroller with these examples to get it to work:
1. SPIFlash / SDFat_format -- format the drive (start over here if it breaks)
2. TinyUSB / msc_external_flash to create a host USB drive. (There were build breaks and I used Library version 2.3.2). Attach the microcontroller to your computer and it should look like a USB flash drive. Copy the file over and pray that it doesn't get corrupted. I had trouble getting the copy to not be zero bytes.
3. I modified the example from BreakoutST7789 to ensure the .png would render.
4. Restore your actual program.

## Materials
Parts, mostly from Adafruit: $50

Load Cell 1KG
https://www.adafruit.com/product/4540

Microcontroller: Adafruit ESP32-S2 Reverse TFT Feather
https://www.adafruit.com/product/5345

24 bit ADC
https://a.co/d/4CLi3f3 SparkFun Qwiic Scale - NAU7802 (I bought this because the Adafruit was out of stock) There are standoffs in the 3d print that match this.

https://www.adafruit.com/product/4538 Adafruit NAU7802 24-Bit ADC (This was out of stock, and will not fit, but it's smaller and could easily work out in the design (I assume))

Connect the ADC to the micro:
https://www.adafruit.com/product/4399

Button:
https://www.adafruit.com/product/3104

Battery:
https://www.adafruit.com/product/1781 (2200mAh)
I kind of wish I'd just used a couple of AAs or a smaller cell.

These cables made it easier to soldier the button to the board:
https://www.adafruit.com/product/261
https://www.adafruit.com/product/3814

Plus an annoying variety of screws, 2xM5 and 2xM4 for the load cell, 2 M2.5 + nuts for the microcontroller. (+M2 but I just left them out)

Plus 2xM2.5mm 6mm standoffs and nuts. https://www.adafruit.com/product/3658
