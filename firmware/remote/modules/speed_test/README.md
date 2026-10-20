# Internet speed

Tests the internet speed of the TV: download, upload and latency.

- **On the TV, not the remote:** the companion app (0.7 or later) runs the
  test on the Google TV Streamer, so it measures what streaming gets. The
  remote's own Wi-Fi couldn't measure more than about 10-20 Mbit/s.
- **Its screen:** **Test now** runs it, in about 15 seconds: latency, then
  download, then upload. The numbers show as it goes, then the result.
  Download is green and upload red (`COL_GOOD`, `COL_BAD`). **Recent**
  lists the last 6 results by time; **Clear** removes them all, after
  asking.
- **Its card:** the last download speed, with upload and latency.

Without the companion app 0.7 the screen says so, and **Test now** is off.

On a poor connection the test still ends with what it could measure
(companion 0.8+). If the remote loses the companion for a moment, the
test goes on; after 90 seconds without an end, the remote calls it
failed.

## How it measures

The companion asks Cloudflare's speed test (speed.cloudflare.com):

- **Latency:** the median time of empty requests over one connection,
  less the time Cloudflare reports spending on them. It sends up to 11 and
  drops those that opened a connection (the first, and any after a lost
  one); it stops after 6 seconds once it has 3, and after 20 seconds in any
  case.
- **Download and upload:** requests that grow (1, 2.5, 10, 25, then 50 MB;
  uploads stop at 25 MB) until each takes about a second, for 6 seconds or
  250 MB each. The 6 seconds cut the last request short. A size Cloudflare
  refuses makes the test step back to the size below. The result is the
  90th percentile of the requests' speeds.
- **Only the transfer is timed** (companion 0.9+): connections are reused,
  a download is timed from its answer's first byte and an upload from its
  connection being up, and the 6 seconds start with the first request's
  first byte. Before, a slow handshake on a poor link counted in, and a
  download of a few Mbit/s could show as a fraction of one.

A test on a fast line moves up to about 500 MB.

## Settings

None. The last results are kept in NVS (`m_speed_test`: `hist`).

## Data and privacy

The test runs from the TV to Cloudflare, which sees the TV's public
address, as with any website. Nothing is published. The results stay on
the remote.

## Credits and licenses

Cloudflare's speed test service (NOTICE); the gauge and arrow icons are
Lucide's.

## Author

the Hubmote authors
