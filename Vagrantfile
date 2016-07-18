# -*- mode: ruby -*-
# vi: set ft=ruby :

# Vagrantfile API/syntax version. Don't touch unless you know what you're doing!
VAGRANTFILE_API_VERSION = "2"


Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|

	config.vm.provider :virtualbox do |vb|
    # memory higher than normal for thrift build
		vb.customize ["modifyvm", :id, "--memory", "4096"]
		vb.customize ["modifyvm", :id, "--cpus", "4"]
	end

	config.vm.define "trusty" do |host|
		$provision = <<-END_SCRIPT
			apt-get update

			# Set the clock and fire up time server to reduce build/sync problems on shared files
			ntpdate  -s time.nist.gov
			apt-get install -y ntp

			apt-get install -y docker.io
			ln -sf /usr/bin/docker.io /usr/local/bin/docker
		END_SCRIPT

		host.vm.box = "trusty64"
		host.vm.box_url = "https://cloud-images.ubuntu.com/vagrant/trusty/current/trusty-server-cloudimg-amd64-vagrant-disk1.box"

		host.vm.provision "shell", inline: $provision

    config.vm.synced_folder "../", "/vagrant"
	end
end
