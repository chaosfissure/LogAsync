#!/usr/bin/env python

import os

if __name__ == '__main__':

	elapsed = []
	processed = []
	msgs = []
	
	for j in xrange(10):
	
		fname = 'thing' + str(j) + '.txt'
		if (os.name == 'nt'):
			os.system('LogAsync.exe > ' + fname)
		else:
			os.system('./LogAsync > ' + fname)
		
		with open(fname, 'r') as f:
			lines = f.readlines()
			splitline = lines[0].strip()[:-2].split(' ')
			
			p = float(splitline[1])
			e = float(splitline[-1])
						
			processed.append(e/p)
			elapsed.append(e)
			msgs.append(p)
		
	averageElapsed = sum(elapsed)/len(elapsed)
	minElapsed = min(elapsed)
	maxElapsed = max(elapsed)

	averageProcessed = sum(processed)/len(processed)
	minProcessed = min(processed)
	maxProcessed = max(processed)
	
	averageCount = sum(msgs)/len(msgs)
	minCount = int(min(msgs))
	maxCount = int(max(msgs))
	
	print '* Elapsed time(ms):'
	print  '\t * Min ({:.8f})'.format(minElapsed),
	print  '| Avg ({:.8f})'.format(averageElapsed),
	print  '| Max ({:.8f})'.format(maxElapsed)
	
	print '* Elapsed time per message(ms):'
	print  '\t * Min ({:,.8f})'.format(minProcessed),
	print  '| Avg ({:,.8f})'.format(averageProcessed),
	print  '| Max ({:,.8f})'.format(maxProcessed)
	
	print '* Total messages processed:'
	print  '\t * Min ({:,})'.format(minCount),
	print  '| Avg ({:,})'.format(averageCount),
	print  '| Max ({:,})'.format(maxCount)
