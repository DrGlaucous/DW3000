# Multi-tracker and anchor DSTWR

This example follows the method outlined on page 250 of the DW3000 user manual.
A single tag will broadcast a ranging request to all anchors within reception range. Those anchors will respond in time, and the tag will send a final response to all of them .*(up to 5, due to the limited size of the packets I'm using in the example, but this can easily be increased if more than 127 bytes are used. 5 trackers should be more than enough for 3D ranging though)*

Any number of tags and anchors can be used in this example, and is more or less a ready-to-go framework for asset tracking. *(within practical reason; too many anchors and tags in one spot and the airwaves will become over-congested; I haven't implemented proper exponential backoff with these, so they'll just keep trying without success)*


