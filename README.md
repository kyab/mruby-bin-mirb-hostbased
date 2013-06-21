# mruby-bin-mirb-hostbased
mruby-bin-mirb-hostbased is mrbgem which allows hostbased irb with mruby.

Compiled executable "mirb-hostbased" is simular to mirb, but mirb-hostbased
works with target board via serial communication.

## Demo
 http://www.youtube.com/watch?v=e8gTTSdxlPU
 
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

# Note
For detailed information about how to compile mruby for chipKIT Max32, check my blog post.

http://d.hatena.ne.jp/kyab/20130201



