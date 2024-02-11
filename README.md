This is a basic terminal based auction house simulator.

It uses GNS (https://github.com/ValveSoftware/GameNetworkingSockets)
The connection part of the system comes from the chat example in that repo.
Reason I used this framework was to make it as close to how I would expect I would work with frameworks.

There is quite a lot of improvements that could be made. Isntead of constantly checking for messages. It could be better to actually make an event based system that only polls them when it actually happens.
Then I would like to change in some places where I use a copy of the variable instead of using pointers.

For database I used Json files as it would make the whole sharing and building of the repo easier. But this should of course be changed to an SQL database ro the likes. For better handeling and safe guardings. As a Json file is quite risky when it comes to overwriting data and so on. But it servered its purpose.

To build the solution you can just run the bat file in the script folder.

## License
- UNLICENSE for this repository (see `UNLICENSE.txt` for more details)
- Premake is licensed under BSD 3-Clause (see included LICENSE.txt file for more details)