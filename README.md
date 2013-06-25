# mruby-bin-mirb-hostbased
mruby-bin-mirb-hostbased is mrbgem which allows hostbased irb with mruby.

Compiled executable "mirb-hostbased" is simular to mirb, but mirb-hostbased
works with target board via serial communication.

## Demo
 http://www.youtube.com/watch?v=e8gTTSdxlPU

 http://www.youtube.com/watch?v=zNeGxjQD1Y0
 
## How it works
As name "hostbased" implies, mirb-hostbased does not send user input directly to
target board, rather, it compiles user input into bytecode on host machine, then send bytecode via serial.

Target boards should handle received bytecode, then send result back to host.
Sample sketch for chipKIT Max32 (and maybe for Arduino Due) is in samples/target.

![Hostbased mirb](https://cacoo.com/diagrams/EmmKpYRK6YEvRwcE-44F09.png)

# Build
## Host side:
Add below line in host Build setting of build_config.rb in mruby's source tree.

```
conf.gem :github => 'kyab/mruby-bin-mirb-hostbased', :branch => 'master'
```

## Target side:
If your board is Arduino compatible, compile and upload sample sketch file in samples/target by your IDE(MPIDE,etc).
I also recommend you to use my [mruby-arduino](https://github.com/kyab/mruby-arduino).

```
conf.gem :github => "kyab/mruby-arduino.git", :branch => "master"
```

# Usage
```
mirb-hostbased -p <port>
```
`<port>` is serial(or USB Serial) port that target connected.

Example:
```
 mirb-hostbased -p /dev/cu.usbserial-A600CKP6
```

Once prompt ">" is displayed, your target is ready. Enter any ruby code like mirb.
```
mirb-hostbased -p /dev/cu.usbserial-A600CKP6 
mirb-hostbased - Hostbased Interactive mruby Shell
  waiting for target on /dev/cu.usbserial-A600CKP6...
(taget):TOTAL_ALLOCATED : 83297
target is ready.
> 1+1
 => 2
> "mruby".upcase.reverse
 => YBURM
> exit
```

## Loading file
You can use ```#file``` special command to read *.rb files.

test.rb
```
def square_add(a,b)
  a*a + b*b
end
puts "test.rb loaded into target"
```

mirb-hostbased:
```
> #file /path/to/test.rb
test.rb loaded into target
 => nil
> square_add(2,3)
 => 25
```

## Reconnect (without reset)
You can reconnect to serial(usb-serial) if your device is self-powered. 
With ```#reconnect``` command or ```--noreset``` option, mirb-hostbased simply reopen serial port.

```
> n=100
 => 100 
> #reconnect
reconnecting to /dev/cu.usbserial-A600CKP6...
> n
 => 100
```

or 
```
> $n=100
 => 100 
> exit
mirb-hostbased --noreset -p /dev/cu.usbserial-A600CKP6
> $n
 => 100
```
restriction: With --noreset option, you cannot reuse local variable in previous mirb-hostbased session.
This is because mirb-hostbased can not restore previous context.

###For Arduino boards:
Usually Arduino boards automatically reset when USB is (re)connected even self-powered from external power source. So to use ```#reconnect``` command or ```--noreset``` option,
you have to disable auto-reset temporarily. 
I recommend to use capacitor hack introduced bellow.
http://electronics.stackexchange.com/questions/24743/arduino-resetting-while-reconnecting-the-serial-terminal.

Your sessions would be like.
```
(Arduino is default and self-powered: auto-reset enabled.)
mirb-hostbased -p /dev/cu.usbserial-A600CKP6
> def foo
*   puts "foo"
* end
 => nil
> n = 99
 => 99
(Disconnect USB.)
(Have fun with target.)
(Connect capacitor between RESET and GND on board. auto-reset disabled.)
(Reconnect USB)
> #reconnect
> foo
foo
 => nil
> n
 => 99
```

```#file``` command and ```#reconnect``` option provide you rapid try-and-test cycle.

# Related articles
##My blog post:
 Hostbased irb Chap.1(Japanese):

 http://d.hatena.ne.jp/kyab/20130621

 Hostbased irb Chap.2(Japanese):
 
 http://d.hatena.ne.jp/kyab/20130625

 How to compile mruby for chipKITMax32(Japanese):
 
 http://d.hatena.ne.jp/kyab/20130201

##Arduino Due mirb(full compiler on board)
 Interactive Arduino Shell - mruby.sh
 
 http://blog.mruby.sh/201305201003.html



