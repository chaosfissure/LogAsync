import socket
import time

IP_ADDR = "10.0.0.1"
PINGBACK_PORT = 5015

sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

while 1:
	try:
		sock2.connect((IP_ADDR, PINGBACK_PORT))
		print 'Connected to ', IP_ADDR
		while 1:
			sock2.send('ping')
			time.sleep(1)

	except KeyboardInterrupt:
		sock2.close()
		print "Termination requested"
		exit(0)
	except Exception as e:
		print time.time(), e