const {spawn} = require('child_process')
const {join} = require('path')
const fs = require('fs')
const nano = require('nanomsg')
const torch = require('torch')
const protobuf = require('protobufjs')
const p = require('util').promisify

const zdbSrc = join(__dirname, '../..')
const zdbDir = join(__dirname, '../run-zdb')
const zdbBin = join(zdbDir, 'zdb')

const zdbInAddress = 'tcp://127.0.0.1:4055'
const zdbOutAddress = 'tcp://127.0.0.1:4056'

describe('zdb', function() {
  this.timeout(6000)
  before('create protobuf message', async function() {

    // get protobuf definitions and input message
    const protoFile = join(__dirname, '../../censys-definitions/proto/hoststore.proto')
    const messageFile = join(__dirname, '../../src/test/data/nanomsg/consume.txt')
    const [jsonMessage, zsearch] = await Promise.all([
      p(fs.readFile)(messageFile, 'utf8'),
      protobuf.load(protoFile)
    ])

    // get the protobuf type we need
    const Record = zsearch.lookupType('zsearch.Record')

    // convert the message body from JSON to protobuf
    const [headerTxt, bodyTxt] = jsonMessage.split('\n')
        , body = JSON.parse(bodyTxt)
    const err = Record.verify(body)
    if (err) throw new Error(err)

    const protoBody = Record.encode(Record.create(body)).finish()
        , protoMessage = Buffer.concat([
            Buffer.from(headerTxt),
            Buffer.from('\n'),
            protoBody
          ])

    // set the message so it's locally accessible
    this.jsonMessage = jsonMessage
    this.protoMessage = protoMessage
  })
  before('compile zdb', function(done) {
    this.timeout(30000)
    const make = spawn('make', [], {stdio: 'inherit', cwd: zdbSrc})
    make.once('close', () => done())
  })
  before('start zdb', function(done) {
    this.zdb = spawn(zdbBin, [], {stdio: 'inherit', cwd: zdbDir})
    setTimeout(done, 3000)
  })
  after('kill zdb', function(done) {
    if (this.zdb) {
      this.zdb.once('close', () => done())
      this.zdb.kill()
    }
  })

  it('should accept sample IP data', function(done) {
    // Given connections to inbound/outbound ports
    const zdbIn = nano.socket('push')
    zdbIn.connect(zdbInAddress)
    const zdbOut = nano.socket('sub')
    zdbOut.connect(zdbOutAddress)

    // I should get a delta
    zdbOut.on('data', (data) => {
      torch.yellow(data.toString())
      done()
    })

    // When I send new data
    torch.blue('sending:\n' + this.protoMessage)
    zdbIn.send(this.protoMessage)
  })
})
