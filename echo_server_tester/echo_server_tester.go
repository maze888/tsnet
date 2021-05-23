package main

import (
	"bytes"
	"flag"
	"fmt"
	"math/rand"
	"net"
	"runtime"
	"strconv"
	"sync"
	"time"
)

func init() {
	rand.Seed(time.Now().UnixNano())
}

const letterBytes = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

func RandString(n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = letterBytes[rand.Intn(len(letterBytes))]
	}
	return b
}

func SendAll(conn net.Conn, data []byte, sendLen int) {
	n := 0

	for sendLen > 0 {
		n, err := conn.Write(data)
		if nil != err {
			panic(err)
		}

		data = data[n:]
		sendLen -= n
	}

	_ = n

	return
}

func ReadAll(conn net.Conn, readLen int) []byte {
	tmpBuffer := make([]byte, 65535)
	readBuffer := make([]byte, 0)

	for readLen > 0 {
		n, err := conn.Read(tmpBuffer)
		if err != nil {
			panic(err)
		}

		readBuffer = append(readBuffer, tmpBuffer[:n]...)
		readLen -= n
	}

	return readBuffer
}

var testFail int = 0

func SendEchoTest(ip string, port int, size int, loop int) {
	defer wait.Done()

	conn, err := net.Dial("tcp", ip+":"+strconv.FormatInt(int64(port), 10))
	if nil != err {
		panic(err)
	}
	defer conn.Close()

	for i := 0; i < loop; i++ {
		sendBuffer := RandString(size)

		SendAll(conn, sendBuffer, size)

		readBuffer := ReadAll(conn, size)

		if !bytes.Equal(sendBuffer, readBuffer) {
			testFail += 1
		}
	}
}

var wait sync.WaitGroup

func main() {
	ip := flag.String("ip", "127.0.0.1", "echo server ip")
	port := flag.Int("port", 0, "echo server port")
	packetSize := flag.Int("size", 1024, "echo packet size")
	loopCount := flag.Int("loop", 1, "number of times to send and receive")
	threadCount := flag.Int("thread", 1, "thread count")

	flag.Parse()

	if *port == 0 {
		panic("invalid port")
	}

	if runtime.GOMAXPROCS(0) != runtime.NumCPU() {
		runtime.GOMAXPROCS(runtime.NumCPU())
	}

	wait.Add(*threadCount)

	for i := 0; i < *threadCount; i++ {
		go SendEchoTest(*ip, *port, *packetSize, *loopCount)
	}

	wait.Wait()

	if testFail > 0 {
		fmt.Printf("test fail: %d count (probably server problem. check server code.)", testFail)
	} else {
		fmt.Printf("test success!\n")
	}
}
