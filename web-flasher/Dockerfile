# Serves the web flasher on port 8000.
#
#   docker build -t muleskin-flasher web-flasher
#   docker run --rm -p 8000:8000 muleskin-flasher
#
# The manifests point at firmware .bin files that the release workflow lays
# down beside them; they are not in the repo. Either drop them into this
# directory before building (they are copied in with everything else), or
# mount a directory of them at /firmware at run time:
#
#   docker run --rm -p 8000:8000 -v "$PWD/bins:/firmware:ro" muleskin-flasher
#
# Web Serial only works in a secure context: http://localhost:8000 is fine,
# a bare LAN IP over plain http is not -- put TLS in front for that.
FROM nginx:1.27-alpine

COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY . /usr/share/nginx/html/
RUN rm /usr/share/nginx/html/nginx.conf && mkdir -p /firmware

EXPOSE 8000
