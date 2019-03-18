from asyncio import create_subprocess_exec, subprocess, get_event_loop
import websockets
import uuid
import json

HOST = '0.0.0.0'
PORT = 8888

RANDOMIZE = './randomization/randomize'
SIMULATE = './simulator/simulator/simulate'

async def process_command(command, data=None):
	process = await create_subprocess_exec(*command.split(), 
										   stdin=subprocess.PIPE, 
										   stdout=subprocess.PIPE,
										   stderr=subprocess.PIPE)
	if data is None:
		stdout, stderr = await process.communicate()
	else:
		stdout, stderr = await process.communicate(input=bytes(str(data), encoding='utf-8'))

	print(f'Return Code: {process.returncode}')
	if stderr:
		print(f'Error: {stderr}')
		print(f'General Process Info: {vars(process)}')

	return stdout.decode()

async def middleware(websocket, path):
	async for request in websocket:

		request = json.loads(request)
		print(f'Request: {json.dumps(request, indent=2)}')
		print()

		if request['type'] == 'randomization':
			result = await process_command(RANDOMIZE)

		elif request['type'] == 'simulation':
			request['id'] = uuid.uuid4().hex
			print(f"Input: {json.dumps(request, indent=2)}")
			print()
			result = await process_command(SIMULATE, request)
		else:
			raise ValueError('Unexpected JSON type.')

		print(f'Output: {json.dumps(result, indent=2)}')
		print()

		await websocket.send(result)

if __name__ == '__main__':
	print(f"Starting websocket server at {HOST}:{PORT}...")
	loop = get_event_loop()
	loop.run_until_complete(websockets.serve(middleware, HOST, PORT))
	loop.run_forever()
