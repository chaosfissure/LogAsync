import socket
import time

IP_ADDR = "10.0.0.1"
IP_PORT = 5001



sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", IP_PORT))
sock.settimeout(2)
sock.listen(1)

i = 0
while 1:
	try:
		conn, addr = sock.accept()
		print 'Connection address:', addr
		sock.send('ping')
		while 1:
			try:
				data = conn.recv(65536)
				i += 1
				if not data:
					break
				else:
					if i % 100 == 0:
						print i, 'received, sending ping...'
						sock.send('ping')
			except KeyboardInterrupt:
				print "Termination requested"
				conn.close()
				exit(0)		
			except Exception as e:
				pass
	except KeyboardInterrupt:
		print "Termination requested"
		exit(0)
	except Exception as e:
		print e, "- Did not connect..."