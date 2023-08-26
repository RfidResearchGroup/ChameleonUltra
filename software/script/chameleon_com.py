import queue
import struct
import threading
import time
import serial
import chameleon_status
import socket


class NotOpenException(Exception):
    """
        Chameleon err status
    """


class OpenFailException(Exception):
    """
        Chameleon open fail(serial port may be error)
    """


class CMDInvalidException(Exception):
    """
        CMD invalid(Unsupported)
    """


class Response:
    """
        Chameleon Response Data
    """

    def __init__(self, cmd, status, data):
        self.cmd = cmd
        self.status = status
        self.data: bytearray = data


class ChameleonTransport:
    def __init__(self):
        pass

    def is_open(self):
        return False

    def open(self):
        raise NotImplementedError('Please implement this')

    def close(self):
        pass

    def write(self, data):
        raise NotImplementedError('Please implement this')

    def read(self):
        raise NotImplementedError('Please implement this')


class ChameleonTCPTransport(ChameleonTransport):
    def __init__(self, connection_string: str):
        self.connection_string = connection_string
        self.socket = None

    def is_open(self):
        return self.socket is not None

    def open(self):
        host, _, port = self.connection_string.rpartition(':')
        self.socket = socket.socket(socket.AF_INET, type=socket.SOCK_STREAM)
        self.socket.connect((host, int(port)))
    
    def close(self):
        self.socket.close()
        self.socket = None
    
    def write(self, data):
        self.socket.send(data)
    
    def read(self, bufsize=1): # Bufsize matching serial
        return self.socket.recv(bufsize)


class ChameleonSerialTransport(ChameleonTransport):
    def __init__(self, port):
        self.port = port
        self.serial_instance = None
    
    def is_open(self):
        return self.serial_instance is not None and self.serial_instance.isOpen()

    def open(self):
        self.serial_instance = serial.Serial(port=self.port, baudrate=115200)

        try:
            self.serial_instance.dtr = 1  # must make dtr enable
        except Exception:
            # not all serial support dtr, e.g. virtual serial over BLE
            pass
        self.serial_instance.timeout = 0  # do not block
    
    def close(self):
        self.serial_instance.close()
        self.serial_instance = None
    
    def write(self, data):
        self.serial_instance.write(data)
    
    def read(self, size=1):
        return self.serial_instance.read(size=size)


class ChameleonCom:
    """
        Chameleon device base class
        Communication and Data frame implemented
    """
    data_frame_sof = 0x11
    data_max_length = 512

    def __init__(self):
        """
            Create a chameleon device instance
        """
        self.serial_instance: serial.Serial | None = None
        self.send_data_queue = queue.Queue()
        self.wait_response_map = {}
        self.event_closing = threading.Event()
        self.transport: ChameleonTransport | None = None

    def is_open(self):
        """
            Chameleon is connected and init.
        :return:
        """
        return self.transport is not None and self.transport.is_open()

    def open(self, transport):
        """
            Open chameleon port to communication
            And init some variables
        :param port: com port, comXXX or ttyXXX
        :return:
        """
        if not self.is_open():
            error = None
            try:
                self.transport = transport
                self.transport.open()
            except Exception as e:
                error = e
            finally:
                if error is not None:
                    raise OpenFailException(error)
            
            # clear variable
            self.send_data_queue.queue.clear()
            self.wait_response_map.clear()
            # Start a sub thread to process data
            self.event_closing.clear()
            threading.Thread(target=self.thread_data_receive).start()
            threading.Thread(target=self.thread_data_transfer).start()
            threading.Thread(target=self.thread_check_timeout).start()
        return self

    def check_open(self):
        """

        :return:
        """
        if not self.is_open():
            raise NotOpenException("Please call open() function to start device.")

    @staticmethod
    def lrc_calc(array):
        """
            calc lrc and auto cut byte
        :param array: value array
        :return: u8 result
        """
        # add and cut byte and return
        ret = 0x00
        for b in array:
            ret += b
            ret &= 0xFF
        return (0x100 - ret) & 0xFF

    def close(self):
        """
            Close chameleon and clear variable
        :return:
        """
        self.event_closing.set()
        try:
            self.transport.close()
        except Exception:
            pass
        finally:
            self.transport = None
        self.wait_response_map.clear()
        self.send_data_queue.queue.clear()

    def thread_data_receive(self):
        """
            SubThread to receive data from chameleon device
        :return:
        """
        data_buffer = bytearray()
        data_position = 0
        data_cmd = 0x0000
        data_status = 0x0000
        data_length = 0x0000

        while self.is_open():
            # receive
            try:
                data_bytes = self.transport.read()
            except Exception as e:
                if not self.event_closing.is_set():
                    print(f"Serial Error {e}, thread for receiver exit.")
                self.close()
                break
            if len(data_bytes) > 0:
                data_byte = data_bytes[0]
                data_buffer.append(data_byte)
                if data_position < 2:  # start of frame
                    if data_position == 0:
                        if data_buffer[data_position] != self.data_frame_sof:
                            print("Data frame no sof byte.")
                            data_position = 0
                            data_buffer.clear()
                            continue
                    if data_position == 1:
                        if data_buffer[data_position] != self.lrc_calc(data_buffer[0:1]):
                            data_position = 0
                            data_buffer.clear()
                            print("Data frame sof lrc error.")
                            continue
                elif data_position == 8:  # frame head lrc
                    if data_buffer[data_position] != self.lrc_calc(data_buffer[0:8]):
                        data_position = 0
                        data_buffer.clear()
                        print("Data frame head lrc error.")
                        continue
                    # frame head complete, cache info
                    data_cmd = struct.unpack(">H", data_buffer[2:4])[0]
                    data_status = struct.unpack(">H", data_buffer[4:6])[0]
                    data_length = struct.unpack(">H", data_buffer[6:8])[0]
                    if data_length > self.data_max_length:
                        data_position = 0
                        data_buffer.clear()
                        print("Data frame data length too than of max.")
                        continue
                elif data_position > 8:  # // frame data
                    if data_position == (8 + data_length + 1):
                        if data_buffer[data_position] == self.lrc_calc(data_buffer[0:-1]):
                            # ok, lrc for data is correct.
                            # and we are receive completed
                            # print(f"Buffer data = {data_buffer.hex()}")
                            if data_cmd in self.wait_response_map:
                                # call processor
                                if 'callback' in self.wait_response_map[data_cmd]:
                                    fn_call = self.wait_response_map[data_cmd]['callback']
                                else:
                                    fn_call = None
                                data_response = data_buffer[9: 9 + data_length]
                                if callable(fn_call):
                                    # delete wait task from map
                                    del self.wait_response_map[data_cmd]
                                    fn_call(data_cmd, data_status, data_response)
                                else:
                                    self.wait_response_map[data_cmd]['response'] = Response(data_cmd, data_status,
                                                                                            data_response)
                            else:
                                print(f"No task wait process: ${data_cmd}")
                        else:
                            print("Data frame finally lrc error.")
                        data_position = 0
                        data_buffer.clear()
                        continue
                data_position += 1
            else:
                time.sleep(0.001)

    def thread_data_transfer(self):
        """
            SubThread to transfer data to chameleon device
        :return:
        """
        while self.is_open():
            # get a task from queue(if exists)
            if self.send_data_queue.empty():
                time.sleep(0.001)
                continue
            task = self.send_data_queue.get()
            task_cmd = task['cmd']
            task_timeout = task['timeout']
            task_close = task['close']
            # register to wait map
            if 'callback' in task and callable(task['callback']):
                self.wait_response_map[task_cmd] = {'callback': task['callback']}  # The callback for this task
            else:
                self.wait_response_map[task_cmd] = {'response': None}
            # set start time
            start_time = time.time()
            self.wait_response_map[task_cmd]['start_time'] = start_time
            self.wait_response_map[task_cmd]['end_time'] = start_time + task_timeout
            self.wait_response_map[task_cmd]['is_timeout'] = False
            try:
                # send to device
                self.transport.write(task['frame'])
            except Exception as e:
                print(f"Serial Error {e}, thread for transfer exit.")
                self.close()
                break
            # update queue status
            self.send_data_queue.task_done()
            # disconnect if DFU command has been sent
            if task_close:
                self.close()

    def thread_check_timeout(self):
        """
            Check task timeout
        :return:
        """
        while self.is_open():
            for task_cmd in self.wait_response_map.keys():
                if time.time() > self.wait_response_map[task_cmd]['end_time']:
                    if 'callback' in self.wait_response_map[task_cmd]:
                        # not sync, call function to notify timeout.
                        self.wait_response_map[task_cmd]['callback'](task_cmd, None, None)
                    else:
                        # sync mode, set timeout flag
                        self.wait_response_map[task_cmd]['is_timeout'] = True
            time.sleep(0.001)

    def make_data_frame_bytes(self, cmd: int, status: int, data: bytearray = None) -> bytearray:
        """
            Make data frame
        :return: frame
        """
        frame = bytearray()
        # sof and sof lrc byte
        frame.append(self.data_frame_sof)
        frame.append(self.lrc_calc(frame[0:1]))
        # head info
        frame.extend(struct.pack('>H', cmd))
        frame.extend(struct.pack('>H', status))
        frame.extend(struct.pack('>H', len(data) if data is not None else 0))
        frame.append(self.lrc_calc(frame[2:8]))
        # data
        if data is not None:
            frame.extend(data)
        # frame lrc
        frame.append(self.lrc_calc(frame))
        return frame

    def send_cmd_auto(self, cmd: int, status: int, data: bytearray = None, callback=None, timeout: int = 3,
                      close: bool = False):
        """
            Send cmd to device
        :param timeout: wait response timeout
        :param cmd: cmd
        :param status: status(optional)
        :param callback: call on response
        :param data: bytes data
        :param close: close connection after executing
        :return:
        """
        self.check_open()
        # delete old task
        if cmd in self.wait_response_map:
            del self.wait_response_map[cmd]
        # make data frame
        data_frame = self.make_data_frame_bytes(cmd, status, data)
        task = {'cmd': cmd, 'frame': data_frame, 'timeout': timeout, 'close': close}
        if callable(callback):
            task['callback'] = callback
        self.send_data_queue.put(task)
        return self

    def send_cmd_sync(self, cmd: int, status: int, data: bytearray or bytes or list or int = None,
                      timeout: int = 3) -> Response:
        """
            Send cmd to device, and block receive data.
        :param cmd: cmd
        :param status: status(optional)
        :param data: bytes data
        :param timeout: wait response timeout
        :return: response data
        """
        if isinstance(data, int):
            data = [data]  # warp array.
        # first to send cmd, no callback mode(sync)
        self.send_cmd_auto(cmd, status, data, None, timeout)
        # wait cmd start process
        while cmd not in self.wait_response_map:
            time.sleep(0.01)
        # wait response data set
        while self.wait_response_map[cmd]['response'] is None:
            if 'is_timeout' in self.wait_response_map[cmd] and self.wait_response_map[cmd]['is_timeout']:
                raise TimeoutError(f"CMD {cmd} exec timeout")
            time.sleep(0.01)
        # ok, data received.
        data_response = self.wait_response_map[cmd]['response']
        del self.wait_response_map[cmd]
        if data_response.status == chameleon_status.Device.STATUS_INVALID_CMD:
            raise CMDInvalidException(f"Device unsupported cmd: {cmd}")
        return data_response


if __name__ == '__main__':
    cml = ChameleonCom().open("com19")
    resp = cml.send_cmd_sync(0x03E9, 0xBEEF, bytearray([0x01, 0x02]))
    print(resp)
