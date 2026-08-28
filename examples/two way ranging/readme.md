# Two Way Ranging Example

This example implements the basic TWR methodology outlined on page 248 of the DW3000 user manual.
The initiator sends out a packet, the responder takes it, records the incoming and outgoing timestamps, and sends it back.

When the initiator gets it, it applies a clock drift compensation and finds the time-of-flight for the pair of transmissions.



