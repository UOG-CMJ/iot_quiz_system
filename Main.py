from paho.mqtt import client
import sys
import json
import asyncio
import wave
from enum import Enum
# from readchar import readkey as getch
# from readchar import key as char_keys
import asyncio
from bleak import BleakScanner, BleakClient, BLEDevice
import logging

class Question:
    def __init__(self, question = "", options = [], answer = -1):
        self.question = question
        self.answer = answer
        self.options = options
        self.type = "Open" if answer == -1 else "MCQ"

class QuestionEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, Question):
            return {"question": obj.question,
                    "options": obj.options,
                    "answer": obj.answer
                }
        # Let the base class default method raise the TypeError
        return super().default(obj)
    
def as_question(dct):
    if 'question' in dct and 'options' in dct and 'answer' in dct:
        return Question(dct['question'], dct['options'], dct['answer'])
    return dct

class modes(Enum):
    EMOTE = "0"
    MCQ_QN = "1"
    OPEN_QN = "2"

questions = [
    Question("MCQ 1 choose 1", ["One", "two"], 0),
    Question("MCQ 2 choose 2", ["One", "two"], 1),
    # Question("MCQ 3 choose 3", ["One", "two", "Three"], 2),
    Question("Open QN 1")
]

current_qn = -1
current_responses = dict()
all_responses = []
mode = modes.EMOTE
students_connected = set()
main_stop_event = asyncio.Event()
ble_device_list = set()
ble_client_list = set()
client_tasks = set()
open_audio_files = dict()

mqttc = client.Client(client.CallbackAPIVersion.VERSION2)
mqttc.username_pw_set("admin", "passwd2")
mqttc.connect("192.168.43.120")
# mqttc.connect("192.168.43.216")
# mqttc.connect("192.168.50.143")
mqttc.connect("127.0.0.1")
mqttc.loop_start()

# mqttc.subscribe("audio")
mqttc.subscribe("+/status", qos=1)
mqttc.subscribe("+/responses", qos=1)
mqttc.subscribe("+/audio", qos=1)

async def ainput() -> str:
    return (await asyncio.to_thread(sys.stdin.readline)).rstrip('\n')

def ble_on_response(sender: BLEDevice, data: bytearray):
    student_id = sender.name.split("#")[-1]
    response = int("".join(map(chr, data)))
    # print(student_id, ": ", "".join(map(chr, data)))
    # print("Length of data: ", len(data))
    current_responses[student_id] = response
    if mode == modes.EMOTE:
        display_emotes()
    if response == 3:
        open_audio_files[student_id] = wave.open(f"{student_id}_qn{current_qn}_audio.wav", "wb")
        open_audio_files[student_id].setsampwidth(2)
        open_audio_files[student_id].setframerate(8000)
        open_audio_files[student_id].setnchannels(1)
    elif response == 4:
        open_audio_files.pop(student_id).close()

def ble_on_audio(sender: BLEDevice, data: bytearray):
    student_id = sender.name.split("#")[-1]
    if student_id in open_audio_files:
        open_audio_files[student_id].writeframes(data)

async def ble_on_connect(device: BLEDevice):
    if device in ble_device_list:
        return
    ble_device_list.add(device)

    def ble_on_disconnect(client: BleakClient):
        ble_client_list.remove(client)
        ble_device_list.remove(device)
        students_connected.remove(device.name.split("#")[-1])

    try:
        async with BleakClient(device, disconnected_callback=ble_on_disconnect) as client:
            ble_client_list.add(client)
            students_connected.add(device.name.split("#")[-1])
            sys.stdout.write(f"\033[s\nstudent {device.name.split("#")[-1]}, has connected, connected {len(students_connected)}\033[u")
            await client.write_gatt_char("01234567-0123-4567-89ab-01234567ffff", mode.value.encode(), response= True)
            await client.start_notify("01234567-0123-4567-89ab-0123456789cd", lambda sender, data: ble_on_response(device, data))
            await client.start_notify("01234567-0123-4567-89ab-01234567aaaa", lambda sender, data: ble_on_audio(device, data))
            await main_stop_event.wait()
    except asyncio.CancelledError as e:
        print("this has been cancelled")
        logging.exception(f'Task was cancelled, details: {asyncio.current_task()}', exc_info=True, stack_info=True)
        raise e

async def scanner_task():
    # stop_event = asyncio.Event()

    def scannerCallback(device, advert_data):
        # print(device)
        client_task = asyncio.ensure_future(ble_on_connect(device))
        client_tasks.add(client_task)
        client_task.add_done_callback(lambda task: client_tasks.remove(client_task))
        
    async with BleakScanner(detection_callback=scannerCallback, service_uuids=["01234567-0123-4567-89ab-0123456789ab"]) as scanner:
        # await stop_event.wait()
        global main_stop_event
        await main_stop_event.wait()
    
    # needed as thread will end without waiting and cancel the tasks instead
    if len(client_tasks) > 0:
        await asyncio.wait(client_tasks, return_when=asyncio.ALL_COMPLETED)

def ble_send_mode_all(modeString):
    def on_complete(task):
        client_tasks.remove(task)
    
    for client in ble_client_list:
        msg_task = asyncio.ensure_future(client.write_gatt_char("01234567-0123-4567-89ab-01234567ffff", modeString, response= True))
        client_tasks.add(msg_task)
        msg_task.add_done_callback(on_complete)
    print("send all complete")

def on_message(client, userdata, message):
    global students_connected
    if len(message.payload) == 0:
        return
    student_id, topic = message.topic.split("/")

    if topic == "responses":
        current_responses[student_id] = int(message.payload)
        if mode == modes.EMOTE:
            display_emotes()
    elif topic == "audio":
        # Need to zero the audio somehow <- secondary, sound can still be heard
        # sys.stdout.write(message.payload)
        sys.stdout.write("\033[s\naudio message got\033[u")
        with wave.open(f"{student_id}_qn{current_qn}_audio.wav", "wb") as wf:
            wf.setsampwidth(2)
            wf.setframerate(8000)
            wf.setnchannels(1)
            wf.writeframes(message.payload)
    elif topic == "status":
        if message.payload == b"1":
            students_connected.add(student_id)
            sys.stdout.write(f"\033[s\nstudent {student_id}, has connected, connected {len(students_connected)}\033[u")
        else:
            students_connected.remove(student_id)
            sys.stdout.write(f"\033[s\nstudent {student_id}, has disconnected, connected {len(students_connected)}\033[u")
    else:
        sys.stdout.write(message.topic, ":", message.payload)

mqttc.on_message = on_message

def display_prompt():
    sys.stdout.write("\033[7;HWhat to do next? ")
    sys.stdout.flush()

def clear_prompt():
    sys.stdout.write("\033[7;H\033[K")
    sys.stdout.flush()

def display_timer(time):
    if (time):
        sys.stdout.write(f"\033[s\033[6;H\033[KTime left:{time}\033[u")
    else:
        sys.stdout.write(f"\033[s\033[6;H\033[KTimes Up!\033[u")
    sys.stdout.flush()

def display_responses():
    if mode != modes.OPEN_QN:
        sys.stdout.write(f"\033[s\033[5;H\033[KAnswers:{len(current_responses)} / {len(students_connected)}\033[u")
        sys.stdout.flush()
    else:
        response_list = list(current_responses.values())
        sys.stdout.write(f"\033[s\033[2;H\033[KRecording/ Transmitting:{response_list.count(3)}\033[u")
        sys.stdout.write(f"\033[s\033[3;H\033[KCompleted:{response_list.count(4)}\033[u")
        sys.stdout.flush()

def display_question(question: Question):
    sys.stdout.write(f"\033[s\033[;H\033[K{question.question}\n")
    for i in range (len(question.options)):
        sys.stdout.write(f"\033[K{i}. {question.options[i]}\n")
    # for answer in question.answers:
    #     sys.stdout.write(f"\033[K{answer}\n")
    sys.stdout.write("\033[u")
    sys.stdout.flush()

def display_answer(question: Question):
    response_list = list(current_responses.values())
    sys.stdout.write(f"\033[s\033[2;H")
    for i in range (len(question.options)):
        count = response_list.count(i)
        correct = "O" if i == question.answer else "X"
        sys.stdout.write(f"\033[K{correct} {i}. {question.options[i]}\t({count})\n")
    sys.stdout.write("\033[u")
    sys.stdout.flush()

def display_emotes():
    sys.stdout.write(f"\033[s\033[1;H\033[KReactions:\n")
    sys.stdout.write(f"\033[KHand raised: {list(current_responses.values()).count(0)}\033[u")
    sys.stdout.flush()

def change_mode(new_mode):
    global mode
    mode = new_mode
    mqttc.publish("mode", f"{mode.value}", qos=1, retain=True)
    ble_send_mode_all(new_mode.value.encode())

async def main():
    global current_qn
    global current_responses
    global all_responses
    global mode
    global students_connected
    ble_server_future = asyncio.ensure_future(asyncio.to_thread(asyncio.run, scanner_task()))

    sys.stdout.write("\033[2J")
    while True:
        sys.stdout.write("\033[2J")
        display_prompt()
        user_input = (await ainput()).upper()
        if user_input == "N":
            clear_prompt()
            current_responses = dict()
            current_qn += 1
            if current_qn > len(questions):
                break
            display_question(questions[current_qn])
            if (questions[current_qn].type == "MCQ"):
                change_mode(modes.MCQ_QN)
            else:
                change_mode(modes.OPEN_QN)
            for i in range(10, -1, -1):
                display_timer(i)
                display_responses()
                await asyncio.sleep(1)
            display_answer(questions[current_qn])
            all_responses.append(current_responses)
            await ainput()
            #sending mode change back to emote
            change_mode(modes.EMOTE)
        elif user_input == "Q":
            mqttc.disconnect()
            with open("results.json", 'w') as f:
                f.write(json.dumps(all_responses))
            break
    main_stop_event.set()
    await ble_server_future
    

if __name__ == "__main__":
    try:
        asyncio.run(main())
    finally:
        for file in open_audio_files.values():
            file.close()
