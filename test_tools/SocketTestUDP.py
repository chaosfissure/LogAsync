import socket
import time

IP_ADDR = "10.0.0.1"
IP_PORT = 5000


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", IP_PORT))
sock.settimeout(2)

i = 0

while 1:
	while 1:
		try:
			data, addr = sock.recvfrom(256)
			print data[:64]
			i += 1
			if i%100 == 0:
				print i, "messages received."
		except KeyboardInterrupt:
			print "Termination requested"
			exit(0)
		except:
			print "No data received - ", time.time()