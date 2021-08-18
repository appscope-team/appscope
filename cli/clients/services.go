package clients

import (
	"context"
	"errors"
	"io"
	"net"

	"github.com/criblio/scope/libscope"
	"github.com/criblio/scope/relay"
	"github.com/mitchellh/mapstructure"
	log "github.com/sirupsen/logrus"
	"golang.org/x/sync/errgroup"
)

// Receiver listens for new unix socket connections and manages clients
// It creates a new goroutine for each client
func Receiver(gctx context.Context, g *errgroup.Group, sq relay.Queue, c *Clients) func() error {
	return func() error {

		log.Info("Receiver routine running")
		defer log.Info("Receiver routine exited")

		l, err := Listen()
		if err != nil {
			return err
		}
		defer l.Close()

		// Listen is a blocking call. The below goroutine makes it non blocking
		// It does not execute in a group context because we don't want to wait for it
		newConns := make(chan net.Conn)
		go clientListener(l, newConns)

		for {
			select {
			case conn := <-newConns:
				if conn == nil {
					return errors.New("Error in Accept")
				}
				client := c.Create(UnixConnection{conn}, libscope.Header{})
				g.Go(clientHandler(gctx, sq, client, c))

			case <-gctx.Done():
				return gctx.Err()
			}
		}
	}
}

// clientListener listens for new clients
// Since Listen is a blocking call, this can be terminated at any time
func clientListener(l net.Listener, newConns chan net.Conn) {
	for {
		conn, err := l.Accept()
		if err != nil {
			// you will need to handle error when received from newConns
			newConns <- nil
			return
		}
		newConns <- conn
	}
}

// clientHandler is a dedicated handler for a unix client
func clientHandler(gctx context.Context, sq relay.Queue, client *Client, c *Clients) func() error {
	return func() error {

		received := make([]byte, 0)

		for {
			buf := make([]byte, 512)
			count, err := client.Conn.Conn.Read(buf)
			if err != nil && err != io.EOF {
				return err
			}
			received = append(received, buf[:count]...)

			// push data to relay sender
			sq <- relay.Message(received)

			if client.ProcessStart.Format == "" {
				var header libscope.Header
				if err := mapstructure.Decode(received, &header); err != nil {
					return err
				}
				if err := c.Update(client.Id, UnixConnection{}, header); err != nil {
					return err
				}
			}

			// case client disconnect:
			// disconnect
			// remove from clients

			select {
			case <-gctx.Done():
				return gctx.Err()
			default:
			}
		}
	}
}
