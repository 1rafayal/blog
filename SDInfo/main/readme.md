#SDInfo
##Intro
These files represent the end product that supports a blog post I created about investigating SD cards over the SPI bus.

##How it works
The code sends various SPI commands over the bus to put an SD card into SPI mode and make it wait for further commands.
The micoro-controller will send these commands regardless of the cards operation, the code will simply report when 
nothing comes back.

If the card is working, it will respond and provide the information on the card itself, speficically including the following:

*The status of the CSD/CID registers
*The capacity
*The manufacturing data

The whole intention behind this was to try and find out if one faulty card that I had could be saved at all, and 
trying to commuinicate with it in this way was the last thing I could think of in order to get something from it.

##What it doesnt do
The code doesnt ask the micro-controller to do anything but send and receive repsonses - it doesnt carry out any
calculations itself. All of the information that it presents to the user comes from the cards own responses. 

Basically, the card does all the work. The micro-controller just presents the responses in a human readable format.

##Blog post
You can read the overly theactrical blog post here: [Raphael's Blog] (https://rafayal.blogspot.com/2026/10/analyzing-broken-sd-cards-with-esp32.html)
