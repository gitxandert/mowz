# mowz: simple keyboard bindings for simple cursor control

This is a simple daemon that reads keyboard input and maps it to mouse actions.

## Install
    git clone https://github.com/gitxandert/mowz.git
    cd mowz
    make && sudo make install

**Note: this doesn't run under the perview of systemd.** All logs are sent to syslog and can be read by running `journalctl -t mowz`.

## Commands
`sudo mowz start`: starts the mowz daemon  
`sudo mowz stop`: stops the mowz daemon  
`mowz / mowz help`: lists mowz's commands, bindings, and tips

## Bindings
Currently, these bindings aren't editable. :) I might change this someday.  
`left_ctl + left_shift + m`: toggle keyboard grabbing  
**Note: normal keyboard actions are turned off while the keyboard is grabbed; only the following will work.**   
`left_ctl (hold)`: increase step factor  
`h`: move left  
`j`: move down  
`k`: move up  
`l`: move right  
`u`: move upper left  
`i`: move upper right  
`n`: move lower left  
`m`: move lower right  
`spacebar`: right-click  
`right_alt`: left-click  
`y`: scroll up  
`b`: scroll down  
`,`: scroll left  
`.`: scroll right

Have fun!
