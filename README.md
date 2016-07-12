# LogAsync
Async Logging is an asynchronous logging system that allows you to control where log data is being logged, as well as provide sets of tags to log your data.  You are able to log data in the following ways:

* As text, rotated at a configurable size.
* As text, rotated at a configurable interval.
* As text, rotated every day at a certain time.
* Continuously appended to a file.
* Sent over UDP sockets.

# Motivation

Logging levels are not adequate to describe lines of code logged to a file.  The amount of logging in a large system can be massive, and the difference of switching from an INFO level to a DEBUG level could result in massive amounts new logs being generated among all source files present in a project.  This makes it hard to track down any data relevant to why you might have switched logging levels, especially if you're only interested in a very small fraction of the data.

LogAsync allows you to further categorize and sort logged lines through additional pieces of information, such as tags.  This allows you to write code that can descriptively tell you the purpose of a log:

`LOG_ASYNC(LOG_INFO, "user", "inventory") << name << " found armor: " << armorName << " with ID " << armorID << std::endl;`

`LOG_ASYNC(LOG_WARNING, "user", "login") << name << " failed " << numAttempts << " login attempts." << std::endl;`

`LOG_ASYNC(LOG_INFO, "user", "login") << name << " has logged into the server" << std::endl;`

`LOG_ASYNC(LOG_INFO, "user", "chat") << name << " wrote:" << chatlog << std::endl;`

Using these tags, it is simple to log all "user" actions to a single location, to capture all "chat" strings in another file -- segmenting related data so it's easier to process.  Since these tags can be used as filters, being descriptive with them will allow you to dynamically group various combinations of tags to a large number of files or sockets.

# Performance

This system demands multithreading and uses busy spinlocks, so it is not sensible to use on a single core machine.

Queues handling the data support 11,000,000 asynchronous logging calls in 3000ms on an i7-2600k, which likely will far exceed the rate at which this data can be offloaded through a network or to a disk.  The logging calls are usually non-blocking and do not directly perform disk or network I/O.

Realistic performance benchmarks will be posted soon.
