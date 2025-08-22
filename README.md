# vtb-cpp-dx11
A Valorant triggerbot based on DirectX 11 and the Interception library.

### Features
DirectX 11 GPU implementation, profiting from hardware acceleration with native C++ speed.
Features aim assistance based on 2D vector averaging.

The aim-assistance only works with sensitivities around 1.0 as 2D != 3D.

### Why does this work with Vanguard?

The interception driver is also used in the raw accel software which is whitelisted in Riots' anticheat solution.

### Why publish this?

As this method was pretty much outdated and i've moved on to genuine hardware input spoofing.
Please note that you need to import libs yourself as I didn't upload the entire project. 

### Disclaimer
Don't use it if you intend to cheat. Riot Vanguard has detection vectors to flag keyboard-based assistance and has hardened their restrictions around Interception.

