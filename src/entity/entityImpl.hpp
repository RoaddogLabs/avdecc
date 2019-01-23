/*
* Copyright (C) 2016-2019, L-Acoustics and its contributors

* This file is part of LA_avdecc.

* LA_avdecc is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* LA_avdecc is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.

* You should have received a copy of the GNU Lesser General Public License
* along with LA_avdecc.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
* @file entityImpl.hpp
* @author Christophe Calmejane
*/

#pragma once

#include "la/avdecc/internals/entity.hpp"
#include "la/avdecc/internals/entityModel.hpp"
#include "la/avdecc/internals/protocolInterface.hpp"
#include "la/avdecc/internals/protocolGenericAecpdu.hpp"
#include "la/avdecc/internals/protocolAemAecpdu.hpp"
#include "la/avdecc/internals/protocolAaAecpdu.hpp"
#include "la/avdecc/internals/protocolMvuAecpdu.hpp"
#include "logHelper.hpp"
#include <cstdint>
#include <algorithm>
#include <mutex>

namespace la
{
namespace avdecc
{
namespace entity
{
template<class SuperClass = LocalEntity>
class LocalEntityImpl : public SuperClass, public protocol::ProtocolInterface::Observer
{
public:
	LocalEntityImpl(protocol::ProtocolInterface* const protocolInterface, typename SuperClass::CommonInformation const& commonInformation, typename SuperClass::InterfacesInformation const& interfacesInformation)
		: SuperClass(commonInformation, interfacesInformation)
		, _protocolInterface(protocolInterface)
	{
		if (!!_protocolInterface->registerLocalEntity(*this))
		{
			throw Exception("Failed to register local entity");
		}
	}

	virtual ~LocalEntityImpl() noexcept override {}

	/* ************************************************************************** */
	/* LocalEntity overrides                                                      */
	/* ************************************************************************** */
	virtual bool enableEntityAdvertising(std::uint32_t const availableDuration, std::optional<model::AvbInterfaceIndex> const interfaceIndex) noexcept override
	{
		std::uint8_t const validTime = static_cast<decltype(validTime)>(availableDuration / 2);
		SuperClass::setValidTime(validTime, interfaceIndex);
		return !_protocolInterface->enableEntityAdvertising(*this, interfaceIndex);
	}

	virtual void disableEntityAdvertising(std::optional<model::AvbInterfaceIndex> const interfaceIndex) noexcept override
	{
		_protocolInterface->disableEntityAdvertising(*this, interfaceIndex);
	}

	/** Sets the entity capabilities and flag for announcement */
	virtual void setEntityCapabilities(EntityCapabilities const entityCapabilities) noexcept override
	{
		std::lock_guard<decltype(_lock)> const lg(_lock);
		SuperClass::setEntityCapabilities(entityCapabilities);
		_protocolInterface->setEntityNeedsAdvertise(*this, typename SuperClass::AdvertiseFlags{ SuperClass::AdvertiseFlag::EntityCapabilities }, std::nullopt);
	}

	/** Sets the association unique identifier and flag for announcement */
	virtual void setAssociationID(UniqueIdentifier const associationID) noexcept override
	{
		std::lock_guard<decltype(_lock)> const lg(_lock);
		SuperClass::setAssociationID(associationID);
		_protocolInterface->setEntityNeedsAdvertise(*this, typename SuperClass::AdvertiseFlags{ SuperClass::AdvertiseFlag::AssociationID }, std::nullopt);
	}

	/** Sets the valid time value on the specified interfaceIndex if set, otherwise on all interfaces, and flag for announcement */
	virtual void setValidTime(std::uint8_t const validTime, std::optional<model::AvbInterfaceIndex> const interfaceIndex = std::nullopt) override
	{
		std::lock_guard<decltype(_lock)> const lg(_lock);
		SuperClass::setValidTime(validTime, interfaceIndex);
		_protocolInterface->setEntityNeedsAdvertise(*this, typename SuperClass::AdvertiseFlags{ SuperClass::AdvertiseFlag::ValidTime }, interfaceIndex);
	}

	/** Sets the gptp grandmaster unique identifier and flag for announcement */
	virtual void setGptpGrandmasterID(UniqueIdentifier const gptpGrandmasterID, model::AvbInterfaceIndex const interfaceIndex) override
	{
		std::lock_guard<decltype(_lock)> const lg(_lock);
		SuperClass::setGptpGrandmasterID(gptpGrandmasterID, interfaceIndex);
		_protocolInterface->setEntityNeedsAdvertise(*this, typename SuperClass::AdvertiseFlags{ SuperClass::AdvertiseFlag::GptpGrandmasterID }, interfaceIndex);
	}

	/** Sets th gptp domain number and flag for announcement */
	virtual void setGptpDomainNumber(std::uint8_t const gptpDomainNumber, model::AvbInterfaceIndex const interfaceIndex) override
	{
		std::lock_guard<decltype(_lock)> const lg(_lock);
		SuperClass::setGptpDomainNumber(gptpDomainNumber, interfaceIndex);
		_protocolInterface->setEntityNeedsAdvertise(*this, typename SuperClass::AdvertiseFlags{ SuperClass::AdvertiseFlag::GptpDomainNumber }, interfaceIndex);
	}

	/* ************************************************************************** */
	/* Public methods                                                             */
	/* ************************************************************************** */
	protocol::ProtocolInterface* getProtocolInterface() noexcept
	{
		return _protocolInterface;
	}

	protocol::ProtocolInterface const* getProtocolInterface() const noexcept
	{
		return _protocolInterface;
	}

	// BasicLockable concept lock method
	virtual void lock() noexcept override
	{
		// We are using the underlying ProtocolInterface's lock, we don't need another lock that might cause deadlocks (the drawback is having to lock the whole ProtocolInterface when accessing the Entity, but it's a small price to pay and almost no performance loss)
		_protocolInterface->lock();
	}

	// BasicLockable concept unlock method
	virtual void unlock() noexcept override
	{
		_protocolInterface->unlock();
	}

	virtual bool isSelfLocked() const noexcept override
	{
		return _protocolInterface->isSelfLocked();
	}

	struct AnswerCallback
	{
		using Callback = std::function<void()>;
		Callback onAnswer{ nullptr };
		// Constructors
		AnswerCallback() = default;
		template<typename T>
		AnswerCallback(T f)
			: onAnswer(std::move(reinterpret_cast<Callback&>(f)))
		{
		}
		// Call operator
		template<typename T, typename... Ts>
		void invoke(Ts&&... params) const noexcept
		{
			if (onAnswer)
			{
				try
				{
					reinterpret_cast<T const&>(onAnswer)(std::forward<Ts>(params)...);
				}
				catch (...)
				{
					// Ignore throws in user handler
				}
			}
		}
	};

	using OnAemAECPErrorCallback = std::function<void(LocalEntity::AemCommandStatus)>;
	using OnAaAECPErrorCallback = std::function<void(LocalEntity::AaCommandStatus)>;
	using OnMvuAECPErrorCallback = std::function<void(LocalEntity::MvuCommandStatus)>;
	using OnACMPErrorCallback = std::function<void(LocalEntity::ControlStatus)>;

	template<typename T, typename Object, typename... Ts>
	static OnAemAECPErrorCallback makeAemAECPErrorHandler(T const& handler, Object const* const object, Ts&&... params)
	{
		if (handler)
			return std::bind(handler, object, std::forward<Ts>(params)...);
		// No handler specified, return an empty handler
		return [](LocalEntity::AemCommandStatus const /*error*/)
		{
		};
	}
	template<typename T, typename Object, typename... Ts>
	static OnAaAECPErrorCallback makeAaAECPErrorHandler(T const& handler, Object const* const object, Ts&&... params)
	{
		if (handler)
			return std::bind(handler, object, std::forward<Ts>(params)...);
		// No handler specified, return an empty handler
		return [](LocalEntity::AaCommandStatus const /*error*/)
		{
		};
	}
	template<typename T, typename Object, typename... Ts>
	static OnMvuAECPErrorCallback makeMvuAECPErrorHandler(T const& handler, Object const* const object, Ts&&... params)
	{
		if (handler)
			return std::bind(handler, object, std::forward<Ts>(params)...);
		// No handler specified, return an empty handler
		return [](LocalEntity::MvuCommandStatus const /*error*/)
		{
		};
	}
	template<typename T, typename Object, typename... Ts>
	static OnACMPErrorCallback makeACMPErrorHandler(T const& handler, Object const* const object, Ts&&... params)
	{
		if (handler)
			return std::bind(handler, object, std::forward<Ts>(params)...);
		// No handler specified, return an empty handler
		return [](LocalEntity::ControlStatus const /*error*/)
		{
		};
	}

	static LocalEntity::AemCommandStatus convertErrorToAemCommandStatus(protocol::ProtocolInterface::Error const error) noexcept;
	static LocalEntity::AaCommandStatus convertErrorToAaCommandStatus(protocol::ProtocolInterface::Error const error) noexcept;
	static LocalEntity::MvuCommandStatus convertErrorToMvuCommandStatus(protocol::ProtocolInterface::Error const error) noexcept;
	static LocalEntity::ControlStatus convertErrorToControlStatus(protocol::ProtocolInterface::Error const error) noexcept;

	static void sendAemAecpCommand(protocol::ProtocolInterface const* const pi, UniqueIdentifier const controllerEntityID, UniqueIdentifier const targetEntityID, networkInterface::MacAddress targetMacAddress, protocol::AemCommandType const commandType, void const* const payload, size_t const payloadLength, std::function<void(la::avdecc::protocol::Aecpdu const*, LocalEntity::AemCommandStatus)> const& onResult) noexcept
	{
		try
		{
			// Build AEM-AECPDU frame
			auto frame = protocol::AemAecpdu::create();
			auto* aem = static_cast<protocol::AemAecpdu*>(frame.get());

			// Set Ether2 fields
			aem->setSrcAddress(pi->getMacAddress());
			aem->setDestAddress(targetMacAddress);
			// Set AECP fields
			aem->setMessageType(protocol::AecpMessageType::AemCommand);
			aem->setStatus(protocol::AecpStatus::Success);
			aem->setTargetEntityID(targetEntityID);
			aem->setControllerEntityID(controllerEntityID);
			// No need to set the SequenceID, it's set by the ProtocolInterface layer
			// Set AEM fields
			aem->setUnsolicited(false);
			aem->setCommandType(commandType);
			aem->setCommandSpecificData(payload, payloadLength);

			auto const error = pi->sendAecpCommand(std::move(frame), targetMacAddress,
				[onResult](la::avdecc::protocol::Aecpdu const* response, protocol::ProtocolInterface::Error const error) noexcept
				{
					utils::invokeProtectedHandler(onResult, response, convertErrorToAemCommandStatus(error));
				});
			if (!!error)
			{
				utils::invokeProtectedHandler(onResult, nullptr, convertErrorToAemCommandStatus(error));
			}
		}
		catch (std::invalid_argument const&)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::AemCommandStatus::ProtocolError);
		}
		catch (...)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::AemCommandStatus::InternalError);
		}
	}

	static void sendAaAecpCommand(protocol::ProtocolInterface const* const pi, UniqueIdentifier const controllerEntityID, UniqueIdentifier const targetEntityID, networkInterface::MacAddress targetMacAddress, addressAccess::Tlvs const& tlvs, std::function<void(la::avdecc::protocol::Aecpdu const*, LocalEntity::AaCommandStatus)> const& onResult) noexcept
	{
		try
		{
			// Build AA-AECPDU frame
			auto frame = protocol::AaAecpdu::create();
			auto* aa = static_cast<protocol::AaAecpdu*>(frame.get());

			// Set Ether2 fields
			aa->setSrcAddress(pi->getMacAddress());
			aa->setDestAddress(targetMacAddress);
			// Set AECP fields
			aa->setMessageType(protocol::AecpMessageType::AddressAccessCommand);
			aa->setStatus(protocol::AecpStatus::Success);
			aa->setTargetEntityID(targetEntityID);
			aa->setControllerEntityID(controllerEntityID);
			// No need to set the SequenceID, it's set by the ProtocolInterface layer
			// Set Address Access fields
			for (auto const& tlv : tlvs)
			{
				aa->addTlv(tlv);
			}

			auto const error = pi->sendAecpCommand(std::move(frame), targetMacAddress,
				[onResult](la::avdecc::protocol::Aecpdu const* response, protocol::ProtocolInterface::Error const error) noexcept
				{
					utils::invokeProtectedHandler(onResult, response, convertErrorToAaCommandStatus(error));
				});
			if (!!error)
			{
				utils::invokeProtectedHandler(onResult, nullptr, convertErrorToAaCommandStatus(error));
			}
		}
		catch (std::invalid_argument const&)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::AaCommandStatus::ProtocolError);
		}
		catch (...)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::AaCommandStatus::InternalError);
		}
	}

	static void sendMvuAecpCommand(protocol::ProtocolInterface const* const pi, UniqueIdentifier const controllerEntityID, UniqueIdentifier const targetEntityID, networkInterface::MacAddress targetMacAddress, protocol::MvuCommandType const commandType, void const* const payload, size_t const payloadLength, std::function<void(la::avdecc::protocol::Aecpdu const*, LocalEntity::MvuCommandStatus)> const& onResult) noexcept
	{
		try
		{
			// Build MVU-AECPDU frame
			auto frame = protocol::MvuAecpdu::create();
			auto* mvu = static_cast<protocol::MvuAecpdu*>(frame.get());

			// Set Ether2 fields
			mvu->setSrcAddress(pi->getMacAddress());
			mvu->setDestAddress(targetMacAddress);
			// Set AECP fields
			mvu->setMessageType(protocol::AecpMessageType::VendorUniqueCommand);
			mvu->setStatus(protocol::AecpStatus::Success);
			mvu->setTargetEntityID(targetEntityID);
			mvu->setControllerEntityID(controllerEntityID);
			// No need to set the SequenceID, it's set by the ProtocolInterface layer
			// Set MVU fields
			mvu->setCommandType(commandType);
			mvu->setCommandSpecificData(payload, payloadLength);

			auto const error = pi->sendAecpCommand(std::move(frame), targetMacAddress,
				[onResult](la::avdecc::protocol::Aecpdu const* response, protocol::ProtocolInterface::Error const error) noexcept
				{
					utils::invokeProtectedHandler(onResult, response, convertErrorToMvuCommandStatus(error));
				});
			if (!!error)
			{
				utils::invokeProtectedHandler(onResult, nullptr, convertErrorToMvuCommandStatus(error));
			}
		}
		catch (std::invalid_argument const&)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::MvuCommandStatus::ProtocolError);
		}
		catch (...)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::MvuCommandStatus::InternalError);
		}
	}

	static void sendAcmpCommand(protocol::ProtocolInterface const* const pi, protocol::AcmpMessageType const messageType, UniqueIdentifier const controllerEntityID, UniqueIdentifier const talkerEntityID, model::StreamIndex const talkerStreamIndex, UniqueIdentifier const listenerEntityID, model::StreamIndex const listenerStreamIndex, uint16_t const connectionIndex, std::function<void(la::avdecc::protocol::Acmpdu const*, LocalEntity::ControlStatus)> const& onResult) noexcept
	{
		try
		{
			// Build ACMPDU frame
			auto frame = protocol::Acmpdu::create();
			auto* acmp = static_cast<protocol::Acmpdu*>(frame.get());

			// Set Ether2 fields
			acmp->setSrcAddress(pi->getMacAddress());
			// No need to set DestAddress, it's always the MultiCast address
			// Set AVTP fields
			acmp->setStreamID(0u);
			// Set ACMP fields
			acmp->setMessageType(messageType);
			acmp->setStatus(protocol::AcmpStatus::Success);
			acmp->setControllerEntityID(controllerEntityID);
			acmp->setTalkerEntityID(talkerEntityID);
			acmp->setListenerEntityID(listenerEntityID);
			acmp->setTalkerUniqueID(talkerStreamIndex);
			acmp->setListenerUniqueID(listenerStreamIndex);
			acmp->setStreamDestAddress({});
			acmp->setConnectionCount(connectionIndex);
			// No need to set the SequenceID, it's set by the ProtocolInterface layer
			acmp->setFlags(ConnectionFlags::None);
			acmp->setStreamVlanID(0);

			auto const error = pi->sendAcmpCommand(std::move(frame),
				[onResult](protocol::Acmpdu const* const response, protocol::ProtocolInterface::Error const error) noexcept
				{
					utils::invokeProtectedHandler(onResult, response, convertErrorToControlStatus(error));
				});
			if (!!error)
			{
				utils::invokeProtectedHandler(onResult, nullptr, convertErrorToControlStatus(error));
			}
		}

		catch (std::invalid_argument const&)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::ControlStatus::ProtocolError);
		}
		catch (...)
		{
			utils::invokeProtectedHandler(onResult, nullptr, LocalEntity::ControlStatus::InternalError);
		}
	}

	static void reflectAecpCommand(protocol::ProtocolInterface* const pi, protocol::Aecpdu const& command, protocol::AecpStatus const status) noexcept
	{
		try
		{
			auto response = command.copy();

			// Set Ether2 fields
			{
				auto& ether2 = static_cast<protocol::EtherLayer2&>(*response);
				if (command.getDestAddress() != pi->getMacAddress())
				{
					LOG_ENTITY_WARN(command.getTargetEntityID(), "Sending AECP response using own MacAddress as source, instead of the incorrect one from the AECP command");
				}
				ether2.setSrcAddress(pi->getMacAddress()); // Using our MacAddress instead of the one from the Command, some devices incorrectly send some AEM messages to the multicast Ether2 MacAddress instead of targeting an entity
				ether2.setDestAddress(command.getSrcAddress());
			}
			// Set AECP fields
			{
				auto& frame = static_cast<protocol::GenericAecpdu&>(*response);
				frame.setMessageType(protocol::AecpMessageType{ static_cast<protocol::AecpMessageType::value_type>(command.getMessageType().getValue() + 1u) }); // Responses are always the value next after the command
				frame.setStatus(status);
			}

			// We don't care about the send errors
			pi->sendAecpResponse(std::move(response), command.getSrcAddress());
		}
		catch (...)
		{
		}
	}

	static void sendAemAecpResponse(protocol::ProtocolInterface* const pi, protocol::AemAecpdu const& commandAem, protocol::AecpStatus const status, void const* const payload, size_t const payloadLength) noexcept
	{
		try
		{
			// Build AEM-AECPDU frame
			auto frame = protocol::AemAecpdu::create();
			auto* aem = static_cast<protocol::AemAecpdu*>(frame.get());

			// Set Ether2 fields
			if (commandAem.getDestAddress() != pi->getMacAddress())
			{
				LOG_ENTITY_WARN(commandAem.getTargetEntityID(), "Sending AEM response using own MacAddress as source, instead of the incorrect one from the AEM command");
			}
			aem->setSrcAddress(pi->getMacAddress()); // Using our MacAddress instead of the one from the Command, some devices incorrectly send some AEM messages to the multicast Ether2 MacAddress instead of targeting an entity
			aem->setDestAddress(commandAem.getSrcAddress());
			// Set AECP fields
			aem->setMessageType(protocol::AecpMessageType::AemResponse);
			aem->setStatus(status);
			aem->setTargetEntityID(commandAem.getTargetEntityID());
			aem->setControllerEntityID(commandAem.getControllerEntityID());
			aem->setSequenceID(commandAem.getSequenceID());
			// Set AEM fields
			aem->setUnsolicited(false);
			aem->setCommandType(commandAem.getCommandType());
			aem->setCommandSpecificData(payload, payloadLength);

			// We don't care about the send errors
			pi->sendAecpResponse(std::move(frame), commandAem.getSrcAddress());
		}
		catch (...)
		{
		}
	}

protected:
	/* ************************************************************************** */
	/* Protected methods                                                          */
	/* ************************************************************************** */
	/** Shutdown method that has to be called by any class inheriting from this one */
	void shutdown() noexcept
	{
		// When shutting down, first disable advertising (sends an ADP DEPARTING message), then remove this local entity from the protocol interface, preventing any incoming message to be processed and dispatched

		// Lock the protocol interface so any incoming message is handled before further processing
		std::lock_guard const lg(*_protocolInterface);

		// Disable advertising
		disableEntityAdvertising(std::nullopt);

		// Unregister local entity
		_protocolInterface->unregisterLocalEntity(*this); // Ignore errors
	}

	/** Called when an AECP command is received and not handled by the LocalEntity. Return true if it has been handled. */
	virtual bool onUnhandledAecpCommand(protocol::ProtocolInterface* const pi, protocol::Aecpdu const& aecpdu) noexcept = 0;

private:
	/* ************************************************************************** */
	/* protocol::ProtocolInterface::Observer overrides                            */
	/* ************************************************************************** */
	/* **** AECP notifications **** */
	virtual void onAecpCommand(protocol::ProtocolInterface* const /*pi*/, LocalEntity const& /*entity*/, protocol::Aecpdu const& aecpdu) noexcept override;

	// Internal variables
	std::recursive_mutex _lock{}; // Lock to protect writable fields (not used for the BasicLockable concept of the class itself)
	protocol::ProtocolInterface* const _protocolInterface{ nullptr }; // Weak reference to the protocolInterface
};

/** Class to be used as final LocalEntityImpl inherited class in order to properly shutdown any inflight messages. */
template<class SuperClass>
class LocalEntityGuard final : public SuperClass
{
public:
	template<typename... Args>
	LocalEntityGuard(protocol::ProtocolInterface* const protocolInterface, Args&&... params)
		: SuperClass(protocolInterface, std::forward<Args>(params)...)
	{
	}
	~LocalEntityGuard() noexcept
	{
		// Shutdown method shall be called first by any class inheriting from LocalEntityImpl.
		// This is to prevent the processing of an incoming message while LocalEntityGuard's parent class (which is LocalEntityImpl's derivated class) has already been destroyed (in LocalEntityImpl's destructor)
		SuperClass::shutdown();
	}
};

/** Entity Capability delegate interface (Controller, Listener, Talker) */
class CapabilityDelegate
{
public:
	using UniquePointer = std::unique_ptr<CapabilityDelegate>;
	virtual ~CapabilityDelegate() = default;

	/* **** Global notifications **** */
	virtual void onControllerDelegateChanged(controller::Delegate* const delegate) noexcept = 0;
	//virtual void onListenerDelegateChanged(listener::Delegate* const delegate) noexcept = 0;
	//virtual void onTalkerDelegateChanged(talker::Delegate* const delegate) noexcept = 0;
	virtual void onTransportError(protocol::ProtocolInterface* const /*pi*/) noexcept {}
	/* **** Discovery notifications **** */
	virtual void onLocalEntityOnline(protocol::ProtocolInterface* const /*pi*/, Entity const& /*entity*/) noexcept {}
	virtual void onLocalEntityOffline(protocol::ProtocolInterface* const /*pi*/, UniqueIdentifier const /*entityID*/) noexcept {}
	virtual void onLocalEntityUpdated(protocol::ProtocolInterface* const /*pi*/, Entity const& /*entity*/) noexcept {}
	virtual void onRemoteEntityOnline(protocol::ProtocolInterface* const /*pi*/, Entity const& /*entity*/) noexcept {}
	virtual void onRemoteEntityOffline(protocol::ProtocolInterface* const /*pi*/, UniqueIdentifier const /*entityID*/) noexcept {}
	virtual void onRemoteEntityUpdated(protocol::ProtocolInterface* const /*pi*/, Entity const& /*entity*/) noexcept {}
	/* **** AECP notifications **** */
	virtual bool onUnhandledAecpCommand(protocol::ProtocolInterface* const /*pi*/, protocol::Aecpdu const& /*aecpdu*/) noexcept
	{
		return false;
	}
	virtual void onAecpUnsolicitedResponse(protocol::ProtocolInterface* const /*pi*/, LocalEntity const& /*entity*/, protocol::Aecpdu const& /*aecpdu*/) noexcept {}
	/* **** ACMP notifications **** */
	virtual void onAcmpSniffedCommand(protocol::ProtocolInterface* const /*pi*/, LocalEntity const& /*entity*/, protocol::Acmpdu const& /*acmpdu*/) noexcept {}
	virtual void onAcmpSniffedResponse(protocol::ProtocolInterface* const /*pi*/, LocalEntity const& /*entity*/, protocol::Acmpdu const& /*acmpdu*/) noexcept {}
};

} // namespace entity
} // namespace avdecc
} // namespace la

// Implementation of LocalEntityImpl template
#include "localEntityImpl.ipp"
