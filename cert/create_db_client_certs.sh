#!/bin/sh

# Create clean environment
rm -rf dbcerts
mkdir dbcerts && cd dbcerts

# Create CA certificate
openssl genrsa 2048 > db-ca-key.pem
openssl req -new -x509 -nodes -days 3600 -key db-ca-key.pem -out db-ca-cert.pem

# Create server certificate, remove passphrase, and sign it
# server-cert.pem = public key, server-key.pem = private key
openssl req -newkey rsa:2048 -days 3600 -nodes -keyout db-server-key.pem -out db-server-req.pem
openssl rsa -in db-server-key.pem -out db-server-key.pem
openssl x509 -req -in db-server-req.pem -days 3600 -CA db-ca-cert.pem -CAkey db-ca-key.pem -set_serial 01 -out db-server-cert.pem

# Create client certificate, remove passphrase, and sign it
# client-cert.pem = public key, client-key.pem = private key
openssl req -newkey rsa:2048 -days 3600 -nodes -keyout db-client-key.pem -out db-client-req.pem
openssl rsa -in db-client-key.pem -out db-client-key.pem
openssl x509 -req -in db-client-req.pem -days 3600 -CA db-ca-cert.pem -CAkey db-ca-key.pem -set_serial 01 -out db-client-cert.pem

#After generating the certificates, verify them:
openssl verify -CAfile db-ca-cert.pem db-server-cert.pem db-client-cert.pem